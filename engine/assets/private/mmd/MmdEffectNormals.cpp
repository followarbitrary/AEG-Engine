#include "MmdEffectNormals.hpp"
#include "material/effects/EffectNormalTexture.hpp"
#include "image/ImageDecoder.hpp"
#include <Utf8Path.hpp>
#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace aeg::assets::detail {
namespace {
std::string_view trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::string folded(std::string_view value) {
    std::string result(value);
    for (auto& c : result)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return result;
}
std::string_view valueWithoutComment(std::string_view value) {
    bool quoted{};
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '"')
            quoted = !quoted;
        else if (!quoted && (value[i] == ';' || value[i] == '#') &&
                 (i == 0 || value[i - 1] == ' ' || value[i - 1] == '\t')) {
            value = value.substr(0, i);
            break;
        }
    }
    if (quoted) throw std::runtime_error("Unterminated EMD effect path quote");
    value = trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
    return value;
}
void warn(ModelData& model, std::string message) {
    if (std::find(model.warnings.begin(), model.warnings.end(), message) == model.warnings.end())
        model.warnings.push_back(std::move(message));
}
}

void applyMmdEffectNormals(ModelData& model) {
    if (model.source.empty()) return;
    auto emd = model.source;
    emd.replace_extension(".emd");
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(emd, filesystemError)) return;
    struct Reference {
        std::string text, error;
    };
    std::optional<Reference> defaultEffect;
    std::map<std::size_t, Reference> overrides;
    try {
        std::istringstream lines(readEffectText(emd));
        std::string storage, section;
        while (std::getline(lines, storage)) {
            auto line = trim(storage);
            if (line.empty() || line.front() == ';' || line.front() == '#') continue;
            if (line.front() == '[') {
                const auto end = line.find(']');
                if (end == std::string_view::npos) throw std::runtime_error("Malformed EMD section");
                section = folded(trim(line.substr(1, end - 1)));
                continue;
            }
            if (section != "effect") continue;
            const auto equals = line.find('=');
            if (equals == std::string_view::npos) {
                warn(model, "Malformed EMD effect assignment: " + std::string(line));
                continue;
            }
            const auto name = folded(trim(line.substr(0, equals)));
            if (name != "obj" && !name.starts_with("obj[")) continue;
            std::optional<std::size_t> materialIndex;
            if (name != "obj") {
                std::size_t index{};
                auto digits = std::string_view(name).substr(4);
                if (digits.empty() || digits.back() != ']') {
                    warn(model, "Malformed EMD material index: " + name);
                    continue;
                }
                digits = trim(digits.substr(0, digits.size() - 1));
                const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), index);
                if (error != std::errc{} || end != digits.data() + digits.size() || index >= model.materials.size()) {
                    warn(model, "Invalid EMD material index: " + name);
                    continue;
                }
                materialIndex = index;
            }
            Reference reference;
            try {
                reference.text = valueWithoutComment(line.substr(equals + 1));
            } catch (const std::runtime_error& error) {
                reference.error = error.what();
            }
            if (materialIndex)
                overrides[*materialIndex] = std::move(reference);
            else
                defaultEffect = std::move(reference);
        }
    } catch (const std::runtime_error& error) {
        for (auto& material : model.materials)
            if (!material.hasNormalMap) material.normalTextureDeclared = true;
        warn(model, "EMD bindings unavailable: " + std::string(error.what()));
        return;
    }
    std::map<std::filesystem::path, EffectNormalTexture> effects;
    for (std::size_t index = 0; index < model.materials.size(); ++index) {
        auto& material = model.materials[index];
        if (material.hasNormalMap || material.normalTextureDeclared) continue;
        const auto found = overrides.find(index);
        if (found == overrides.end() && !defaultEffect) continue;
        const auto& bindingReference = found == overrides.end() ? *defaultEffect : found->second;
        const auto& reference = bindingReference.text;
        material.normalTextureDeclared = true;
        if (bindingReference.error.empty() && (reference.empty() || folded(reference) == "none")) continue;
        try {
            if (!bindingReference.error.empty()) throw std::runtime_error(bindingReference.error);
            const auto path = resolveEffectReference(reference, emd.parent_path());
            auto [position, inserted] = effects.try_emplace(path);
            if (inserted) position->second = readEffectNormalTexture(path);
            const auto& binding = position->second;
            if (binding.image.empty()) throw std::runtime_error(binding.diagnostic);
            auto image = readTexture(binding.image);
            material.normalTextureSource = foundation::pathToUtf8(binding.image);
            material.normalTexture = std::move(image);
            material.normalScale = 1;
            material.hasNormalMap = true;
        } catch (const std::runtime_error& error) {
            warn(model, "EMD normal texture for material " + std::to_string(index) + " ('" + material.name +
                            "') unavailable; keeping vertex normals: " + error.what());
        }
    }
}
}
