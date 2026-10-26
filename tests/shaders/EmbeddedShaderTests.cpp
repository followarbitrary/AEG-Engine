#include <ShaderLibrary.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using aeg::render::detail::embeddedShader;
using aeg::render::detail::ShaderId;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::byte> readReference(const std::filesystem::path& path) {
    // File IO is confined to this test; production shaders must use static storage.
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(bool(input), "cannot open reference SPIR-V: " + path.string());
    const auto length = input.tellg();
    require(length > 0, "empty reference SPIR-V: " + path.string());
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(input), "cannot read complete reference SPIR-V: " + path.string());
    return bytes;
}

void checkShader(ShaderId id, const std::filesystem::path& reference) {
    const auto bytes = embeddedShader(id);
    require(bytes.size() >= 5 * sizeof(std::uint32_t), "missing SPIR-V header");
    require(bytes.size() % sizeof(std::uint32_t) == 0, "SPIR-V byte size must contain whole words");
    require(reinterpret_cast<std::uintptr_t>(bytes.data()) % alignof(std::uint32_t) == 0,
            "SPIR-V storage must be uint32_t aligned");
    std::uint32_t magic = 0;
    std::memcpy(&magic, bytes.data(), sizeof(magic));
    require(magic == 0x07230203u, "invalid SPIR-V magic word");
    const auto expected = readReference(reference);
    require(bytes.size() == expected.size(), "embedded and compiled SPIR-V sizes differ");
    require(std::equal(bytes.begin(), bytes.end(), expected.begin()),
            "embedded SPIR-V bytes differ from the build intermediate");
    const auto repeated = embeddedShader(id);
    require(bytes.data() == repeated.data() && bytes.size() == repeated.size(),
            "repeated shader lookup must return the same static storage");
}

void checkUnknownId(ShaderId id) {
    try {
        (void)embeddedShader(id);
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("unknown ShaderId must throw std::invalid_argument");
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: aeg_embedded_shader_tests compiled-shaders-directory\n";
        return 1;
    }
    const std::filesystem::path directory(argv[1]);
    const std::array shaders{
        std::pair{ShaderId::ModelVertex, "model.vert.spv"},
        std::pair{ShaderId::ModelFragment, "model.frag.spv"},
        std::pair{ShaderId::FullscreenVertex, "fullscreen.vert.spv"},
        std::pair{ShaderId::FxaaFragment, "fxaa.frag.spv"},
        std::pair{ShaderId::TaaFragment, "taa.frag.spv"},
        std::pair{ShaderId::ModelTaaVertex, "model_taa.vert.spv"},
        std::pair{ShaderId::ModelTaaFragment, "model_taa.frag.spv"},
        std::pair{ShaderId::ShadowVertex, "shadow.vert.spv"},
        std::pair{ShaderId::ShadowFragment, "shadow.frag.spv"},
    };
    int failures = 0;
    for (const auto& [id, filename] : shaders) {
        try {
            checkShader(id, directory / filename);
            std::cout << "PASS embedded " << filename << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL embedded " << filename << ": " << error.what() << '\n';
        }
    }
    try {
        checkUnknownId(static_cast<ShaderId>(-1));
        checkUnknownId(static_cast<ShaderId>(shaders.size()));
        std::cout << "PASS unknown shader IDs\n";
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL unknown shader IDs: " << error.what() << '\n';
    }
    return failures == 0 ? 0 : 1;
}
