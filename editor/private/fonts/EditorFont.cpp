#include "EditorFont.hpp"
#include <Utf8Path.hpp>
#include <imgui.h>
#include "../EditorWidgets.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace aeg::editor {
namespace {
std::filesystem::path windowsFontsDirectory() {
#ifdef _WIN32
    std::wstring directory(512, L'\0');
    for (;;) {
        const auto size = GetWindowsDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
        if (!size) return {};
        if (size < directory.size()) {
            directory.resize(size);
            return std::filesystem::path(directory) / L"Fonts";
        }
        directory.resize(static_cast<std::size_t>(size) + 1);
    }
#else
    return {};
#endif
}

std::filesystem::path selectFont(const std::filesystem::path& resources, const std::filesystem::path& requested) {
    std::error_code error;
    if (!requested.empty()) {
        if (!std::filesystem::is_regular_file(requested, error))
            throw std::runtime_error("Chinese font file does not exist: " + foundation::pathToUtf8(requested));
        return std::filesystem::absolute(requested).lexically_normal();
    }
    std::vector<std::filesystem::path> candidates;
    for (const auto* name : {"NotoSansSC-Regular.ttf", "NotoSansCJKsc-Regular.otf", "SourceHanSansSC-Regular.otf"})
        candidates.push_back(resources / "assets" / "fonts" / name);
    const auto system = windowsFontsDirectory();
    if (!system.empty())
        for (const auto* name : {L"msyh.ttc", L"msjh.ttc", L"simhei.ttf", L"simsun.ttc"})
            candidates.push_back(system / name);
    for (const auto& candidate : candidates) {
        error.clear();
        if (std::filesystem::is_regular_file(candidate, error))
            return std::filesystem::absolute(candidate).lexically_normal();
    }
    throw std::runtime_error("No Chinese font found. Use --font PATH with a Chinese TTF/OTF/TTC font, "
                             "or install a Windows Chinese font.");
}

void validateFontContainer(std::span<const unsigned char> bytes) {
    const auto invalid = [] { throw std::runtime_error("Invalid or unsupported font table data"); };
    const auto within = [&](std::size_t offset, std::size_t length) {
        return offset <= bytes.size() && length <= bytes.size() - offset;
    };
    const auto read16 = [&](std::size_t offset) -> std::uint16_t {
        if (!within(offset, 2)) invalid();
        return (std::uint16_t(bytes[offset]) << 8) | bytes[offset + 1];
    };
    const auto read32 = [&](std::size_t offset) -> std::uint32_t {
        if (!within(offset, 4)) invalid();
        return (std::uint32_t(bytes[offset]) << 24) | (std::uint32_t(bytes[offset + 1]) << 16) |
               (std::uint32_t(bytes[offset + 2]) << 8) | bytes[offset + 3];
    };
    const auto tag = [](const char* value) -> std::uint32_t {
        return (std::uint32_t(value[0]) << 24) | (std::uint32_t(value[1]) << 16) | (std::uint32_t(value[2]) << 8) |
               std::uint32_t(value[3]);
    };
    std::size_t face = 0;
    if (read32(0) == tag("ttcf")) {
        const auto version = read32(4);
        const auto count = read32(8);
        if ((version != 0x00010000 && version != 0x00020000) || count == 0 || count > bytes.size() / 4 ||
            !within(12, std::size_t(count) * 4))
            invalid();
        for (std::size_t i = 0; i < count; ++i)
            if (!within(read32(12 + i * 4), 12)) invalid();
        face = read32(12); // ImGui's default FontNo selects the first TTC face.
    }
    const auto format = read32(face);
    if (format != 0x00010000 && format != tag("OTTO") && format != tag("true")) invalid();
    const auto count = read16(face + 4);
    if (!count || !within(face + 12, std::size_t(count) * 16)) invalid();
    struct Table {
        std::uint32_t name;
        std::size_t offset, length;
    };
    std::vector<Table> tables;
    tables.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto record = face + 12 + i * 16;
        Table table{read32(record), read32(record + 8), read32(record + 12)};
        if (!within(table.offset, table.length)) invalid();
        for (const auto& previous : tables)
            if (previous.name == table.name) invalid();
        tables.push_back(table);
    }
    const auto find = [&](const char* name, std::size_t minimum) -> Table {
        for (const auto& table : tables)
            if (table.name == tag(name)) {
                if (table.length < minimum) invalid();
                return table;
            }
        throw std::runtime_error("Invalid or unsupported font table data");
    };
    const auto head = find("head", 54);
    const auto hhea = find("hhea", 36);
    const auto maxp = find("maxp", 6);
    const auto cmap = find("cmap", 4);
    const auto glyphs = read16(maxp.offset + 4);
    const auto metrics = read16(hhea.offset + 34);
    if (read32(head.offset + 12) != 0x5F0F3CF5 || !glyphs || !metrics || metrics > glyphs) invalid();
    (void)find("hmtx", std::size_t(metrics) * 4 + std::size_t(glyphs - metrics) * 2);
    const auto encodings = read16(cmap.offset + 2);
    if (!encodings || 4 + std::size_t(encodings) * 8 > cmap.length) invalid();
    bool unicode = false;
    for (std::size_t i = 0; i < encodings; ++i) {
        const auto record = cmap.offset + 4 + i * 8;
        const auto platform = read16(record), encoding = read16(record + 2);
        const auto offset = read32(record + 4);
        if (offset >= cmap.length || cmap.length - offset < 2) invalid();
        if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10))) unicode = true;
    }
    if (!unicode) invalid();
    if (format == tag("OTTO")) {
        (void)find("CFF ", 4); // CFF2/variable outlines are not handled by this backend.
    } else {
        const auto glyf = find("glyf", 0);
        const auto locationFormat = read16(head.offset + 50);
        if (locationFormat > 1) invalid();
        const auto stride = locationFormat == 0 ? 2u : 4u;
        const auto loca = find("loca", (std::size_t(glyphs) + 1) * stride);
        std::size_t previous = 0;
        for (std::size_t i = 0; i <= glyphs; ++i) {
            const auto offset =
                stride == 2 ? std::size_t(read16(loca.offset + i * 2)) * 2 : std::size_t(read32(loca.offset + i * 4));
            if (offset < previous || offset > glyf.length) invalid();
            previous = offset;
        }
    }
}

std::vector<unsigned char> readFont(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    if (!input || length < 256 || length > 64 * 1024 * 1024)
        throw std::runtime_error("Invalid font file size (expected 256 bytes to 64 MiB): " +
                                 foundation::pathToUtf8(path));
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read font file: " + foundation::pathToUtf8(path));
    const bool trueType = bytes[0] == 0 && bytes[1] == 1 && bytes[2] == 0 && bytes[3] == 0;
    const bool supported = trueType || std::memcmp(bytes.data(), "ttcf", 4) == 0 ||
                           std::memcmp(bytes.data(), "OTTO", 4) == 0 || std::memcmp(bytes.data(), "true", 4) == 0;
    if (!supported) throw std::runtime_error("Invalid or unsupported font data: " + foundation::pathToUtf8(path));
    // ImGui's stb backend asserts on a broken SFNT/TTC before Build can report
    // failure. Reject invalid directories and essential table bounds first.
    // This is a bounded container preflight, not a complete outline parser.
    try {
        validateFontContainer(bytes);
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string(error.what()) + ": " + foundation::pathToUtf8(path));
    }
    return bytes;
}

// Static storage is required: ImGui retains this range pointer in its config.
constexpr ImWchar chineseRanges[] = {0x0020, 0x00FF, // Latin and Latin supplement.
                                     0x2000, 0x206F, // General punctuation, including em dash and ellipsis.
                                     0x3000, 0x30FF, // CJK punctuation and Kana, also useful for PMX names.
                                     0x31F0, 0x31FF, 0x3400, 0x4DBF, // CJK Extension A.
                                     0x4E00, 0x9FFF,                 // Simplified and Traditional Chinese ideographs.
                                     0xFF00, 0xFFEF, 0xFFFD, 0xFFFD, 0};
}

EditorFontInfo configureEditorFont(const std::filesystem::path& resources, const std::filesystem::path& requested) {
    if (!ImGui::GetCurrentContext()) throw std::runtime_error("Editor font needs an active ImGui context");
    auto& io = ImGui::GetIO();
    if (!io.Fonts->Fonts.empty())
        throw std::runtime_error("Editor font must be configured before renderer initialization");
    const auto source = selectFont(resources, requested);
    const auto bytes = readFont(source);
    auto* owned = IM_ALLOC(bytes.size());
    if (!owned) throw std::bad_alloc();
    std::memcpy(owned, bytes.data(), bytes.size());
    ImFontConfig config;
    config.FontDataOwnedByAtlas = true;
    config.OversampleH = 1;
    config.OversampleV = 1;
    std::memcpy(config.Name, "AEG Chinese UI", sizeof("AEG Chinese UI"));
    io.Fonts->TexDesiredWidth = 4096;
    io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
    const float pixels = 18.0f * ui::dpi();
    auto* font = io.Fonts->AddFontFromMemoryTTF(owned, static_cast<int>(bytes.size()), pixels, &config, chineseRanges);
    if (!font || !io.Fonts->Build())
        throw std::runtime_error("Cannot build Chinese font atlas: " + foundation::pathToUtf8(source));
    for (const ImWchar glyph : {ImWchar(0x4E2D), ImWchar(0x6587), ImWchar(0x6A21), ImWchar(0x578B)})
        if (!font->FindGlyphNoFallback(glyph))
            throw std::runtime_error("Selected font does not contain Chinese glyphs: " +
                                     foundation::pathToUtf8(source));
    io.FontDefault = font;
    return {source, pixels};
}
}
