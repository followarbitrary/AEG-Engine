#pragma once
#include <cstdint>

namespace aeg::render {
inline constexpr std::uint32_t kMaxShadowCascades = 4;
struct ShadowSettings {
    bool enabled{false};
    std::uint32_t lightIndex{0};    // One selected directional or spot light; point shadows require six faces.
    std::uint32_t resolution{1024}; // Power of two, [16,4096], also bounded by device capability.
    float depthBias{0.0005f};       // Bias in normalized shadow depth.
    float slopeBias{0.002f};
    std::uint32_t cascadeCount{4};    // Directional: 1 preserves whole-model fitting, 2..4 split the camera frustum.
    float splitLambda{0.5f};          // Blend between linear (0) and logarithmic (1) depth splits.
    float maxDistance{0};             // Positive camera-view distance, or zero to use the camera far plane.
    float blendFraction{0.1f};        // Fraction of each cascade interval blended into the next, [0,0.3].
    float distanceFadeFraction{0.1f}; // Fraction of the last interval faded to unshadowed, [0,0.5].
    std::uint32_t pcfRadius{1};       // 0: hard shadow, 1: 3x3 PCF, 2: 5x5 PCF.
    bool operator==(const ShadowSettings&) const = default;
};
}
