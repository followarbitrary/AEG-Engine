#pragma once
#include <math.hpp>
#include <cstdint>

namespace aeg::render {
inline constexpr std::uint32_t kMaxLights = 8;
enum class LightType : std::uint32_t { Directional, Point, Spot };

struct Light {
    LightType type{LightType::Directional};
    math::Vec3 position{};
    math::Vec3 direction{-0.5f, -1.0f, -0.4f}; // World-space direction in which light travels.
    math::Vec3 color{1};                       // Linear RGB, multiplied by relative intensity.
    float intensity{3};
    float range{10};                            // Positive finite world-space radius for point and spot lights.
    float innerConeAngle{math::to_radians(20)}; // Half angles, in radians.
    float outerConeAngle{math::to_radians(30)};
    bool enabled{true};
    bool operator==(const Light&) const = default;
};
}
