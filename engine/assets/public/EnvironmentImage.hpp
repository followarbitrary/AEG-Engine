#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace aeg::assets {
// Top row first, equirectangular 2:1 image. Linear radiance, four floats per pixel.
// +Y is the north pole; longitude atan2(z,x) maps to U=angle/(2*pi)+0.5.
struct HdrImageData {
    std::uint32_t width{}, height{};
    std::vector<float> rgba;
};
// Radiance RGBE (.hdr) decoding only; LDR input is rejected rather than gamma-converted.
HdrImageData loadHdrImage(const std::filesystem::path& path);
HdrImageData makeProceduralEnvironment();
}
