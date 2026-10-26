#include <EnvironmentImage.hpp>
#include <Utf8Path.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
using aeg::assets::loadHdrImage;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template <class F> void rejected(F&& call, const char* message) {
    try {
        call();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}
struct Directory {
    fs::path path = fs::temp_directory_path() /
                    ("aeg-hdr-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() { fs::create_directory(path); }
    ~Directory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};
void writeBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(bool(file), "write complete HDR fixture");
}
std::string hdr(std::uint32_t width, std::uint32_t height) {
    return "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " + std::to_string(height) + " +X " + std::to_string(width) + "\n";
}
void linearHdrAndUnicodePath() {
    Directory directory;
    const auto path = directory.path / aeg::foundation::pathFromUtf8("中文 环境.hdr");
    auto bytes = hdr(4, 2);
    // RGBE (128,32,16,130) is exactly linear RGB (2,.5,.25).
    for (int i = 0; i < 8; ++i) {
        const std::array<unsigned char, 4> pixel =
            i == 4 ? std::array<unsigned char, 4>{0, 128, 0, 129} : std::array<unsigned char, 4>{128, 32, 16, 130};
        bytes.append(reinterpret_cast<const char*>(pixel.data()), pixel.size());
    }
    writeBytes(path, bytes);
    const auto image = loadHdrImage(path);
    require(image.width == 4 && image.height == 2 && image.rgba.size() == 32, "retain equirectangular dimensions");
    require(image.rgba[0] == 2 && image.rgba[1] == 0.5f && image.rgba[2] == 0.25f && image.rgba[3] == 1,
            "HDR values remain linear and above one without an LDR gamma conversion");
    require(image.rgba[16] == 0 && image.rgba[17] == 1 && image.rgba[18] == 0, "retain top-first HDR row order");
}
void rleAndTruncation() {
    Directory directory;
    const auto path = directory.path / "scanlines.hdr";
    auto bytes = hdr(8, 4);
    for (int row = 0; row < 4; ++row) {
        const unsigned char scanline[]{2, 2, 0, 8, 136, 128, 136, 32, 136, 16, 136, 130};
        bytes.append(reinterpret_cast<const char*>(scanline), sizeof(scanline));
    }
    writeBytes(path, bytes);
    const auto image = loadHdrImage(path);
    require(image.rgba.size() == 128 && image.rgba[0] == 2 && image.rgba[126] == .25f, "decode modern RGBE RLE rows");
    bytes.pop_back();
    writeBytes(path, bytes);
    rejected([&] { (void)loadHdrImage(path); }, "truncated HDR RLE payload must fail");
    auto raw = hdr(4, 2);
    raw.append(3, '\0');
    writeBytes(path, raw);
    rejected([&] { (void)loadHdrImage(path); }, "truncated legacy raw HDR payload must fail");
}
void rejectInvalidInputs() {
    Directory directory;
    const auto path = directory.path / "invalid.hdr";
    writeBytes(path, "P6\n2 1\n255\n" + std::string(6, '\xff'));
    rejected([&] { (void)loadHdrImage(path); }, "LDR input must not be silently reinterpreted as linear HDR");
    writeBytes(path, hdr(4, 4) + std::string(64, '\0'));
    rejected([&] { (void)loadHdrImage(path); }, "environment input must be 2:1");
    writeBytes(path, hdr(2147483646u, 1073741823u));
    rejected([&] { (void)loadHdrImage(path); }, "huge dimensions must be rejected before decode allocation");
    writeBytes(path, {});
    rejected([&] { (void)loadHdrImage(path); }, "empty input must fail");
    rejected([&] { (void)loadHdrImage(directory.path / "missing.hdr"); }, "missing input must fail");
}
void proceduralSkyIsUsableAndDeterministic() {
    const auto a = aeg::assets::makeProceduralEnvironment(), b = aeg::assets::makeProceduralEnvironment();
    require(a.width >= 16 && a.width == a.height * 2 &&
                a.rgba.size() == static_cast<std::size_t>(a.width) * a.height * 4,
            "procedural sky has valid equirectangular data");
    require(a.width == b.width && a.height == b.height && a.rgba == b.rgba, "procedural sky is deterministic");
    float maximum = 0;
    for (std::size_t i = 0; i < a.rgba.size(); ++i) {
        require(std::isfinite(a.rgba[i]) && a.rgba[i] >= 0, "procedural radiance is finite and nonnegative");
        if (i % 4 == 3)
            require(a.rgba[i] == 1, "procedural alpha is one");
        else
            maximum = std::max(maximum, a.rgba[i]);
    }
    require(maximum > 1 && maximum < 65504, "procedural sky provides HDR illumination within renderer range");
}
}
int main() {
    int failures = 0;
    const std::pair<const char*, std::function<void()>> tests[]{
        {"linear HDR and Unicode paths", linearHdrAndUnicodePath},
        {"RLE and truncation", rleAndTruncation},
        {"invalid HDR input", rejectInvalidInputs},
        {"procedural environment", proceduralSkyIsUsableAndDeterministic}};
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
