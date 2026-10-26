#include "EffectNormalTexture.hpp"
#include <Utf8Path.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace aeg::assets::detail {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t maximumFileBytes = 8 * 1024 * 1024;
std::string_view trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::string key(const fs::path& path) {
    auto value = foundation::pathToUtf8(path.lexically_normal());
    for (auto& c : value)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}
void appendUtf8(std::string& result, std::uint32_t c) {
    if (c <= 0x7f)
        result += static_cast<char>(c);
    else if (c <= 0x7ff) {
        result += static_cast<char>(0xc0 | (c >> 6));
        result += static_cast<char>(0x80 | (c & 63));
    } else if (c <= 0xffff) {
        result += static_cast<char>(0xe0 | (c >> 12));
        result += static_cast<char>(0x80 | ((c >> 6) & 63));
        result += static_cast<char>(0x80 | (c & 63));
    } else {
        result += static_cast<char>(0xf0 | (c >> 18));
        result += static_cast<char>(0x80 | ((c >> 12) & 63));
        result += static_cast<char>(0x80 | ((c >> 6) & 63));
        result += static_cast<char>(0x80 | (c & 63));
    }
}
bool validUtf8(std::string_view value) {
    for (std::size_t i = 0; i < value.size();) {
        const auto lead = static_cast<unsigned char>(value[i++]);
        if (lead < 0x80) continue;
        const unsigned count = lead >= 0xf0 && lead <= 0xf4
                                   ? 3
                                   : (lead >= 0xe0 && lead <= 0xef ? 2 : (lead >= 0xc2 && lead <= 0xdf ? 1 : 0));
        if (!count || value.size() - i < count) return false;
        std::uint32_t c = lead & ((1u << (6 - count)) - 1);
        for (unsigned n = 0; n < count; ++n) {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0) != 0x80) return false;
            c = (c << 6) | (next & 63);
        }
        if ((count == 1 && c < 0x80) || (count == 2 && c < 0x800) || (count == 3 && c < 0x10000) || c > 0x10ffff ||
            (c >= 0xd800 && c <= 0xdfff))
            return false;
    }
    return true;
}
std::string unescapePath(std::string_view text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = text[i];
        if (c == '\\' && i + 1 < text.size() && (text[i + 1] == '\\' || text[i + 1] == '"')) c = text[++i];
        if (static_cast<unsigned char>(c) < 32) throw std::runtime_error("Control character in effect resource path");
        result += c;
    }
    return result;
}
std::string stripComments(std::string_view text) {
    std::string result;
    bool block{}, quoted{}, escaped{}, line{};
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i], next = i + 1 < text.size() ? text[i + 1] : '\0';
        if (line) {
            if (c == '\n') {
                line = false;
                result += c;
            } else
                result += ' ';
        } else if (block) {
            if (c == '*' && next == '/') {
                block = false;
                result += "  ";
                ++i;
            } else
                result += c == '\n' ? '\n' : ' ';
        } else if (quoted) {
            result += c;
            if (!escaped && c == '"') quoted = false;
            if (!escaped && c == '\\')
                escaped = true;
            else
                escaped = false;
        } else if (c == '/' && next == '/') {
            line = true;
            result += "  ";
            ++i;
        } else if (c == '/' && next == '*') {
            block = true;
            result += "  ";
            ++i;
        } else {
            result += c;
            quoted = c == '"';
        }
    }
    if (block || quoted) throw std::runtime_error("Unterminated effect comment or string");
    return result;
}
bool identifierStart(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool identifierCharacter(char c) { return identifierStart(c) || (c >= '0' && c <= '9'); }
std::string token(std::string_view& text) {
    text = trim(text);
    std::size_t length{};
    while (length < text.size() && identifierCharacter(text[length])) ++length;
    auto result = std::string(text.substr(0, length));
    text.remove_prefix(length);
    return result;
}
struct Macro {
    std::string value;
    bool uncertain{}, function{};
};
using Macros = std::map<std::string, Macro>;
enum class Truth { no, yes, unknown };
Truth negate(Truth value) { return value == Truth::unknown ? value : (value == Truth::yes ? Truth::no : Truth::yes); }
Truth both(Truth a, Truth b) {
    if (a == Truth::no || b == Truth::no) return Truth::no;
    return a == Truth::yes && b == Truth::yes ? Truth::yes : Truth::unknown;
}
Truth either(Truth a, Truth b) {
    if (a == Truth::yes || b == Truth::yes) return Truth::yes;
    return a == Truth::no && b == Truth::no ? Truth::no : Truth::unknown;
}
using Number = std::optional<std::int64_t>;
Truth truth(Number n) { return !n ? Truth::unknown : (*n ? Truth::yes : Truth::no); }
Number number(Truth n) { return n == Truth::unknown ? Number{} : Number{n == Truth::yes ? 1 : 0}; }
bool compoundGuardMacro(std::string_view value) {
    unsigned parentheses{};
    bool operand{};
    for (const auto c : trim(value)) {
        if (c == '(') {
            ++parentheses;
            operand = true;
        } else if (c == ')') {
            if (parentheses) --parentheses;
        } else if (!parentheses) {
            if (std::string_view("|&=<>*/%?^,").find(c) != std::string_view::npos ||
                ((c == '+' || c == '-') && operand))
                return true;
            if (c != ' ' && c != '\t' && c != '!' && c != '~' && c != '+' && c != '-') operand = true;
        }
    }
    return false;
}
class Expression {
public:
    Expression(std::string_view text, const Macros& macros, unsigned depth = 0)
        : text_(text), macros_(macros), depth_(depth) {}
    Number evaluate() {
        auto result = logicalOr();
        return trim(text_).empty() && !unsupportedMacro_ ? result : Number{};
    }

private:
    std::string_view text_;
    const Macros& macros_;
    unsigned depth_;
    unsigned nesting_{};
    bool unsupportedMacro_{};
    bool take(std::string_view value) {
        text_ = trim(text_);
        if (!text_.starts_with(value)) return false;
        text_.remove_prefix(value.size());
        return true;
    }
    Number logicalOr() {
        auto a = logicalAnd();
        while (take("||")) {
            const auto b = logicalAnd();
            a = number(either(truth(a), truth(b)));
        }
        return a;
    }
    Number logicalAnd() {
        auto a = equality();
        while (take("&&")) {
            const auto b = equality();
            a = number(both(truth(a), truth(b)));
        }
        return a;
    }
    Number equality() {
        auto a = relation();
        for (;;) {
            const bool equal = take("==");
            if (!equal && !take("!=")) return a;
            const auto b = relation();
            a = a && b ? Number{equal ? *a == *b : *a != *b} : Number{};
        }
    }
    Number relation() {
        auto a = unary();
        for (;;) {
            std::string_view op;
            for (const auto candidate : {">=", "<=", ">", "<"})
                if (take(candidate)) {
                    op = candidate;
                    break;
                }
            if (op.empty()) return a;
            auto b = unary();
            if (!a || !b)
                a = {};
            else if (op == ">=")
                a = *a >= *b;
            else if (op == "<=")
                a = *a <= *b;
            else if (op == ">")
                a = *a > *b;
            else
                a = *a < *b;
        }
    }
    Number unary() {
        if (++nesting_ > 64) throw std::runtime_error("Effect conditional expression exceeds nesting limit");
        struct Pop {
            unsigned& depth;
            ~Pop() { --depth; }
        } pop{nesting_};
        if (take("!")) return number(negate(truth(unary())));
        if (take("+")) return unary();
        if (take("-")) {
            const auto n = unary();
            return n && *n != INT64_MIN ? Number{-*n} : Number{};
        }
        if (take("(")) {
            auto value = logicalOr();
            return take(")") ? value : Number{};
        }
        text_ = trim(text_);
        if (text_.empty() || depth_ > 16) return {};
        if (identifierStart(text_.front())) {
            auto name = token(text_);
            if (name == "defined") {
                const bool bracket = take("(");
                name = token(text_);
                if (name.empty() || (bracket && !take(")"))) return {};
                const auto found = macros_.find(name);
                return found == macros_.end() ? Number{0} : (found->second.uncertain ? Number{} : Number{1});
            }
            const auto found = macros_.find(name);
            if (found == macros_.end()) return 0;
            if (found->second.uncertain) {
                unsupportedMacro_ = true;
                return {};
            }
            if (found->second.function) return {};
            // A compound replacement is a token sequence, not an atomic value.
            // Without full preprocessor expansion its precedence is ambiguous.
            if (compoundGuardMacro(found->second.value)) {
                unsupportedMacro_ = true;
                return {};
            }
            Expression nested(found->second.value, macros_, depth_ + 1);
            const auto result = nested.evaluate();
            unsupportedMacro_ |= nested.unsupportedMacro_;
            return result;
        }
        const bool hex = text_.starts_with("0x") || text_.starts_with("0X");
        if (hex) text_.remove_prefix(2);
        const bool octal = !hex && text_.starts_with('0');
        std::int64_t value{};
        const auto [end, error] =
            std::from_chars(text_.data(), text_.data() + text_.size(), value, hex ? 16 : (octal ? 8 : 10));
        if (error != std::errc{}) {
            text_ = {};
            return {};
        }
        text_.remove_prefix(static_cast<std::size_t>(end - text_.data()));
        while (!text_.empty() &&
               (text_.front() == 'u' || text_.front() == 'U' || text_.front() == 'l' || text_.front() == 'L'))
            text_.remove_prefix(1);
        return value;
    }
};
std::string expanded(std::string_view value, const Macros& macros) {
    std::set<std::string> seen;
    for (unsigned depth = 0; depth < 16; ++depth) {
        value = trim(value);
        if (value.empty()) throw std::runtime_error("Empty normal texture declaration");
        if (value.front() == '"') {
            bool escaped{};
            std::size_t end = 1;
            for (; end < value.size(); ++end) {
                const auto c = value[end];
                if (!escaped && c == '"') break;
                if (!escaped && c == '\\')
                    escaped = true;
                else
                    escaped = false;
            }
            if (end >= value.size() || !trim(value.substr(end + 1)).empty())
                throw std::runtime_error("Normal texture must resolve to a single quoted string");
            return std::string(value.substr(1, end - 1));
        }
        auto remainder = value;
        const auto name = token(remainder);
        if (name.empty() || !trim(remainder).empty() || !seen.insert(name).second)
            throw std::runtime_error("Unsupported or cyclic normal texture macro");
        const auto found = macros.find(name);
        if (found == macros.end() || found->second.uncertain || found->second.function)
            throw std::runtime_error("Unresolved or conditional normal texture macro");
        value = found->second.value;
    }
    throw std::runtime_error("Normal texture macro expansion exceeds limit");
}
class EffectReader {
public:
    EffectNormalTexture read(const fs::path& entry) {
        scan(entry, Truth::yes, 0);
        const auto enabled = macros_.find("USE_NORMALMAP");
        if (uncertainInclude_ || (enabled != macros_.end() && enabled->second.uncertain))
            throw std::runtime_error("Unsupported conditional normal texture binding");
        if (enabled == macros_.end()) throw std::runtime_error("Effect does not enable USE_NORMALMAP");
        const auto path = macros_.find("TEXTURE_NORMALMAP");
        if (path == macros_.end() || path->second.uncertain)
            throw std::runtime_error("Effect has no unambiguous TEXTURE_NORMALMAP declaration");
        if (const auto scale = macros_.find("NormalMapResolution"); scale != macros_.end())
            resolution(scale->second.value, scale->second.uncertain ? Truth::unknown : Truth::yes);
        if (unsupportedResolution_)
            throw std::runtime_error("Unsupported normal-map UV tiling (NormalMapResolution must be 1)");
        return {resolveEffectReference(expanded(path->second.value, macros_), entry.parent_path(), true), {}};
    }

private:
    struct Conditional {
        Truth parent{}, taken{};
        bool otherwise{};
    };
    Macros macros_;
    std::set<std::string> activeFiles_;
    std::size_t files_{}, bytes_{};
    bool uncertainInclude_{}, unsupportedResolution_{};
    void resolution(std::string_view value, Truth active) {
        value = trim(value);
        if (!value.empty() && (value.back() == 'f' || value.back() == 'F')) value.remove_suffix(1);
        float scale{};
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), scale);
        unsupportedResolution_ = active == Truth::unknown || error != std::errc{} ||
                                 end != value.data() + value.size() || !std::isfinite(scale) || scale != 1;
    }
    void scan(const fs::path& filename, Truth initial, unsigned depth) {
        if (depth >= 16 || ++files_ > 64) throw std::runtime_error("Effect include count/depth exceeds limit");
        const auto fileKey = key(filename);
        if (!activeFiles_.insert(fileKey).second) throw std::runtime_error("Cyclic effect include");
        struct Pop {
            std::set<std::string>& paths;
            std::string value;
            ~Pop() { paths.erase(value); }
        } pop{activeFiles_, fileKey};
        auto text = readEffectText(filename);
        bytes_ += text.size();
        if (bytes_ > 16 * 1024 * 1024) throw std::runtime_error("Effect include bytes exceed limit");
        // Translation-phase splicing removes bytes even within identifiers,
        // strings and continued comments; inserting spaces changes their meaning.
        std::string spliced;
        spliced.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                if (text[i + 1] == '\n') {
                    ++i;
                    continue;
                }
                if (text[i + 1] == '\r' && i + 2 < text.size() && text[i + 2] == '\n') {
                    i += 2;
                    continue;
                }
            }
            spliced += text[i];
        }
        text = stripComments(spliced);
        std::istringstream lines(text);
        std::vector<Conditional> conditions;
        Truth active = initial;
        std::string storage;
        while (std::getline(lines, storage)) {
            auto line = trim(storage);
            if (line.starts_with('#')) {
                line.remove_prefix(1);
                const auto directive = token(line);
                line = trim(line);
                if (directive == "if" || directive == "ifdef" || directive == "ifndef") {
                    Truth condition;
                    if (directive == "if")
                        condition = truth(Expression(line, macros_).evaluate());
                    else {
                        const auto name = token(line);
                        if (name.empty() || !trim(line).empty())
                            throw std::runtime_error("Malformed effect conditional");
                        const auto macro = macros_.find(name);
                        condition = macro == macros_.end() ? Truth::no
                                                           : (macro->second.uncertain ? Truth::unknown : Truth::yes);
                        if (directive == "ifndef") condition = negate(condition);
                    }
                    if (conditions.size() >= 128) throw std::runtime_error("Effect conditional depth exceeds limit");
                    conditions.push_back({active, condition, false});
                    active = both(active, condition);
                } else if (directive == "else" || directive == "elif") {
                    if (conditions.empty() || conditions.back().otherwise)
                        throw std::runtime_error("Malformed effect conditional branch");
                    auto& frame = conditions.back();
                    const auto next = directive == "else" ? Truth::yes : truth(Expression(line, macros_).evaluate());
                    active = both(frame.parent, both(negate(frame.taken), next));
                    frame.taken = either(frame.taken, next);
                    frame.otherwise = directive == "else";
                } else if (directive == "endif") {
                    if (conditions.empty()) throw std::runtime_error("Unmatched effect endif");
                    active = conditions.back().parent;
                    conditions.pop_back();
                } else if (active != Truth::no && (directive == "define" || directive == "undef")) {
                    const auto name = token(line);
                    if (name.empty()) throw std::runtime_error("Malformed effect macro");
                    if (directive == "undef" && active == Truth::yes)
                        macros_.erase(name);
                    else
                        macros_[name] = {std::string(trim(line)), active == Truth::unknown,
                                         directive == "define" && line.starts_with('(')};
                } else if (active != Truth::no && directive == "include") {
                    if (active == Truth::unknown) {
                        uncertainInclude_ = true;
                        continue;
                    }
                    const auto reference = expanded(line, macros_);
                    scan(resolveEffectReference(reference, filename.parent_path(), true), active, depth + 1);
                }
            } else if (active != Truth::no) {
                constexpr std::string_view name = "NormalMapResolution";
                const auto position = line.find(name);
                if (position == std::string_view::npos || !trim(line.substr(0, position)).ends_with("float")) continue;
                if (position + name.size() < line.size() && identifierCharacter(line[position + name.size()])) continue;
                auto rest = trim(line.substr(position + name.size()));
                if (!rest.starts_with('=')) {
                    unsupportedResolution_ = true;
                    continue;
                }
                rest.remove_prefix(1);
                const auto end = rest.find(';');
                if (end == std::string_view::npos) {
                    unsupportedResolution_ = true;
                    continue;
                }
                resolution(rest.substr(0, end), active);
            }
        }
        if (!conditions.empty()) throw std::runtime_error("Unterminated effect conditional");
    }
};
}

std::string readEffectText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Effect metadata file cannot be opened: " + foundation::pathToUtf8(path));
    const auto size = file.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(static_cast<std::streamoff>(size)) > maximumFileBytes)
        throw std::runtime_error("Effect metadata file exceeds byte limit");
    std::string text(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(text.data(), static_cast<std::streamsize>(text.size())))
        throw std::runtime_error("Effect metadata file is incomplete");
    if (text.starts_with("\xef\xbb\xbf")) return text.substr(3);
    const bool little = text.starts_with("\xff\xfe"), big = text.starts_with("\xfe\xff");
    if (!little && !big) return text; // Legacy comment bytes are discarded lexically, not interpreted as paths.
    if ((text.size() - 2) % 2) throw std::runtime_error("Invalid UTF16 effect metadata");
    std::string result;
    const auto unit = [&](std::size_t i) -> std::uint32_t {
        const auto a = static_cast<unsigned char>(text[i]), b = static_cast<unsigned char>(text[i + 1]);
        return little ? a | (std::uint32_t(b) << 8) : (std::uint32_t(a) << 8) | b;
    };
    for (std::size_t i = 2; i < text.size(); i += 2) {
        auto c = unit(i);
        if (c >= 0xd800 && c <= 0xdbff) {
            if (i + 3 >= text.size()) throw std::runtime_error("Invalid UTF16 surrogate in effect metadata");
            const auto low = unit(i + 2);
            if (low < 0xdc00 || low > 0xdfff) throw std::runtime_error("Invalid UTF16 surrogate in effect metadata");
            c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00;
            i += 2;
        } else if (c >= 0xdc00 && c <= 0xdfff)
            throw std::runtime_error("Invalid UTF16 surrogate in effect metadata");
        if (!c) throw std::runtime_error("NUL in UTF16 effect metadata");
        appendUtf8(result, c);
    }
    return result;
}

fs::path resolveEffectReference(std::string_view reference, const fs::path& directory, bool stringEscapes) {
    reference = trim(reference);
    if (reference.empty() || reference.find('\0') != std::string_view::npos)
        throw std::runtime_error("Empty/NUL effect resource path");
    std::vector<std::string> decoded;
    if (validUtf8(reference)) decoded.emplace_back(reference);
#ifdef _WIN32
    else {
        for (const auto page : std::array<unsigned, 2>{CP_ACP, 932}) {
            const auto count = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, reference.data(),
                                                   static_cast<int>(reference.size()), nullptr, 0);
            if (count <= 0) continue;
            std::wstring wide(static_cast<std::size_t>(count), L'\0');
            if (!MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, reference.data(), static_cast<int>(reference.size()),
                                     wide.data(), count))
                continue;
            decoded.push_back(foundation::pathToUtf8(fs::path(wide)));
        }
    }
#endif
    if (decoded.empty()) throw std::runtime_error("Unsupported effect path encoding");
    std::map<std::string, fs::path> possible, existing;
    for (auto value : decoded) {
        if (stringEscapes) value = unescapePath(value);
        std::replace(value.begin(), value.end(), '\\', '/');
        auto path = (directory / foundation::pathFromUtf8(value)).lexically_normal();
        std::error_code error;
        path = fs::absolute(path, error).lexically_normal();
        if (error) continue;
        possible.try_emplace(key(path), path);
        if (fs::is_regular_file(path, error)) existing.try_emplace(key(path), path);
    }
    if (existing.size() > 1) throw std::runtime_error("Ambiguous legacy effect path encoding");
    if (!existing.empty()) return existing.begin()->second;
    if (possible.empty()) throw std::runtime_error("Effect resource path cannot be resolved");
    return possible.begin()->second;
}

EffectNormalTexture readEffectNormalTexture(const fs::path& effect) {
    try {
        return EffectReader{}.read(effect);
    } catch (const std::runtime_error& error) {
        return {{}, error.what()};
    }
}
}
