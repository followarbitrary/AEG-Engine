#include <EditorFont.hpp>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Context {
    ImGuiContext* value{ImGui::CreateContext()};
    Context() { require(value != nullptr, "create ImGui context"); }
    ~Context() { ImGui::DestroyContext(value); }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};

struct TemporaryDirectory {
    fs::path path = fs::temp_directory_path() /
        ("aeg-font-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() {
        require(fs::create_directory(path), "create unique temporary font directory");
    }
    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
};

fs::path installedFont(std::initializer_list<const char*> names) {
#ifdef _WIN32
    std::array<wchar_t, MAX_PATH + 1> windows{};
    const auto length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    require(length > 0 && length < windows.size(), "locate installed Windows font fixtures");
    const fs::path directory = fs::path(windows.data()) / "Fonts";
#else
    const fs::path directory = "C:/Windows/Fonts";
#endif
    for (const auto* name : names) {
        const auto candidate = directory / name;
        if (fs::is_regular_file(candidate)) return candidate;
    }
    throw std::runtime_error("font test requires an installed Windows font fixture");
}

void requireChineseFont(const aeg::editor::EditorFontInfo& info) {
    auto& io = ImGui::GetIO();
    require(!info.source.empty() && fs::is_regular_file(info.source), "report a real selected font source");
    require(io.FontDefault != nullptr, "install the selected font as the ImGui default");
    require(io.Fonts->IsBuilt() && io.Fonts->TexWidth > 0 && io.Fonts->TexHeight > 0,
            "build the font atlas before renderer initialization");
    require(std::isfinite(info.pixelSize) && info.pixelSize > 0 &&
                std::abs(io.FontDefault->FontSize - info.pixelSize) < 0.01f,
            "report the size of the installed font");
    // These are characters used in editor labels and model paths. A fallback
    // question mark also has positive width, so measurement alone is insufficient.
    constexpr ImWchar characters[] = {
        'A', 0x4E2D, 0x6587, 0x573A, 0x666F, 0x6A21, 0x578B, 0x5BFC, 0x5165, 0x8DEF, 0x5F84, 0x3002, 0x2014};
    for (const auto character : characters) {
        const auto* glyph = io.FontDefault->FindGlyphNoFallback(character);
        require(glyph != nullptr && glyph != io.FontDefault->FallbackGlyph,
                "editor text must use real Chinese, Latin and punctuation glyphs");
        require(glyph->AdvanceX > 0 && glyph->Visible, "editor glyph has visible geometry and advance");
    }
    const char* label = "A \xE4\xB8\xAD\xE6\x96\x87 \xE6\xA8\xA1\xE5\x9E\x8B";
    const auto size = io.FontDefault->CalcTextSizeA(info.pixelSize, FLT_MAX, 0, label);
    require(std::isfinite(size.x) && size.x > info.pixelSize && size.y > 0,
            "measure a mixed UTF-8 editor label");
}

std::string thrownMessage(const std::function<void()>& action) {
    try {
        action();
    } catch (const std::exception& error) {
        std::string message = error.what();
        require(!message.empty(), "font failures explain their cause");
        std::transform(message.begin(), message.end(), message.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        require(message.find("font") != std::string::npos, "font failure identifies the resource");
        return message;
    }
    throw std::runtime_error("invalid requested font must throw a standard exception");
}

void automaticFont() {
    TemporaryDirectory resources;
    Context context;
    const auto info = aeg::editor::configureEditorFont(resources.path);
    requireChineseFont(info);
}

void unicodeExplicitFont() {
    TemporaryDirectory resources;
    const auto directory = resources.path / fs::path(u8"\u4e2d\u6587\u5b57\u4f53 with spaces");
    fs::create_directory(directory);
    const auto requested = directory / fs::path(u8"\u7f16\u8f91\u5668\u5b57\u4f53.ttc");
    fs::copy_file(installedFont({"msyh.ttc", "msjh.ttc", "simhei.ttf", "simsun.ttc"}), requested);
    Context context;
    const auto info = aeg::editor::configureEditorFont(resources.path, requested);
    require(fs::equivalent(info.source, requested), "explicit Unicode font path takes precedence over installed fonts");
    requireChineseFont(info);
    require(fs::remove(requested), "font loader releases the source file");
    require(ImGui::GetIO().Fonts->Build(), "atlas retains owned font data after source file removal");
    require(ImGui::GetIO().FontDefault->FindGlyphNoFallback(0x4E2D) != nullptr,
            "retained font data rebuilds Chinese glyphs");
}

void missingExplicitFont() {
    TemporaryDirectory resources;
    Context context;
    const auto message = thrownMessage([&] {
        (void)aeg::editor::configureEditorFont(resources.path, resources.path / "absent-font.ttf");
    });
    require(message.find("absent-font.ttf") != std::string::npos,
            "missing explicit font diagnostic identifies the requested file");
}

void invalidExplicitFont() {
    TemporaryDirectory resources;
    const auto requested = resources.path / "garbage-font.ttf";
    {
        std::ofstream stream(requested, std::ios::binary);
        const std::array<char, 200> bytes{};
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        require(stream.good(), "write malformed font fixture");
    }
    Context context;
    const auto message = thrownMessage([&] { (void)aeg::editor::configureEditorFont(resources.path, requested); });
    require(message.find("invalid") != std::string::npos || message.find("supported") != std::string::npos ||
                message.find("load") != std::string::npos,
            "malformed font diagnostic explains the load failure");
}

using FontBytes = std::array<unsigned char, 256>;

void bigEndian16(FontBytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = static_cast<unsigned char>(value >> 8);
    bytes.at(offset + 1) = static_cast<unsigned char>(value);
}

void bigEndian32(FontBytes& bytes, std::size_t offset, std::uint32_t value) {
    bigEndian16(bytes, offset, static_cast<std::uint16_t>(value >> 16));
    bigEndian16(bytes, offset + 2, static_cast<std::uint16_t>(value));
}

void malformedDirectoryFont(const char* filename, const FontBytes& bytes) {
    TemporaryDirectory resources;
    const auto requested = resources.path / filename;
    {
        std::ofstream stream(requested, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        require(stream.good(), "write malformed font directory fixture");
    }
    Context context;
    const auto message = thrownMessage([&] { (void)aeg::editor::configureEditorFont(resources.path, requested); });
    require(message.find("invalid") != std::string::npos || message.find("malformed") != std::string::npos ||
                message.find("unsupported") != std::string::npos,
            "malformed font directory reports a standard invalid-font error");
}

void zeroTableFont() {
    FontBytes bytes{};
    bigEndian32(bytes, 0, 0x00010000);
    malformedDirectoryFont("zero-tables.ttf", bytes);
}

void outOfBoundsCollectionFace() {
    FontBytes bytes{};
    bigEndian32(bytes, 0, 0x74746366); // ttcf
    bigEndian32(bytes, 4, 0x00010000);
    bigEndian32(bytes, 8, 1);
    bigEndian32(bytes, 12, 512);
    malformedDirectoryFont("outside-face.ttc", bytes);
}

void outOfBoundsFontTable() {
    FontBytes bytes{};
    bigEndian32(bytes, 0, 0x00010000);
    bigEndian16(bytes, 4, 1);
    bigEndian16(bytes, 6, 16);
    bigEndian32(bytes, 12, 0x68656164); // head
    bigEndian32(bytes, 20, 248);
    bigEndian32(bytes, 24, 16);
    malformedDirectoryFont("outside-table.ttf", bytes);
}

void latinOnlyExplicitFont() {
    TemporaryDirectory resources;
    Context context;
    const auto message = thrownMessage([&] {
        (void)aeg::editor::configureEditorFont(resources.path, installedFont({"arial.ttf"}));
    });
    require(message.find("chinese") != std::string::npos || message.find("cjk") != std::string::npos ||
                message.find("glyph") != std::string::npos,
            "Latin-only font diagnostic explains the missing Chinese coverage");
}
}

int main() {
    int failures = 0;
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"automatic font supports Chinese editor text", automaticFont},
        {"explicit Unicode font source and atlas ownership", unicodeExplicitFont},
        {"missing explicit font reports an error", missingExplicitFont},
        {"malformed explicit font reports an error", invalidExplicitFont},
        {"SFNT with zero tables reports an error", zeroTableFont},
        {"TTC with out-of-bounds face reports an error", outOfBoundsCollectionFace},
        {"SFNT with out-of-bounds table reports an error", outOfBoundsFontTable},
        {"Latin-only explicit font reports missing Chinese glyphs", latinOnlyExplicitFont}};
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
