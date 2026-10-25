#include "PmxValidation.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace aeg::assets::detail {
bool hasPmxSignature(const std::filesystem::path& path) {
    std::array<char, 4> signature{};
    std::ifstream input(path, std::ios::binary);
    return input.read(signature.data(), signature.size()) &&
           signature == std::array<char, 4>{'P', 'M', 'X', ' '};
}
void validatePmxHeader(const std::filesystem::path& path) {
    // Assimp's PMX reader uses these sizes before it can validate a scene.
    // Check the fixed header first; conversion references are checked by our
    // tracked MMD importer patch. This is not a replacement PMX parser.
    std::array<unsigned char, 17> header{};
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(header.data()), header.size()))
        throw std::runtime_error("PMX header is truncated");
    if (header[0] != 'P' || header[1] != 'M' || header[2] != 'X' || header[3] != ' ')
        throw std::runtime_error("Invalid PMX signature");
    const auto versionBits = std::uint32_t(header[4]) | (std::uint32_t(header[5]) << 8) |
                             (std::uint32_t(header[6]) << 16) | (std::uint32_t(header[7]) << 24);
    const float version = std::bit_cast<float>(versionBits);
    if (version != 2.0f && version != 2.1f)
        throw std::runtime_error("Unsupported PMX version; expected 2.0 or 2.1");
    if (header[8] != 8 || header[9] > 1 || header[10] > 4)
        throw std::runtime_error("Invalid PMX header size, encoding or additional UV count");
    for (std::size_t i = 11; i < header.size(); ++i)
        if (header[i] != 1 && header[i] != 2 && header[i] != 4)
            throw std::runtime_error("Invalid PMX index width");
}
}
