#pragma once
#include <cstdint>

namespace aeg::render {
struct ShadowSettings {
    bool enabled{false};
    std::uint32_t lightIndex{0};    // One selected directional or spot light; point shadows require six faces.
    std::uint32_t resolution{1024}; // Power of two, [16,4096], also bounded by device capability.
    float depthBias{0.0005f};       // Bias in normalized shadow depth.
    float slopeBias{0.002f};
    bool operator==(const ShadowSettings&) const = default;
};
}
