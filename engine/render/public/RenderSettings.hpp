#pragma once
#include <math.hpp>
#include <RenderCamera.hpp>
#include <Light.hpp>
#include <array>
#include <stdexcept>

namespace aeg::render {
struct RenderSettings {
    aeg::math::Mat4 model{aeg::math::identity()};
    aeg::math::Mat4 viewProjection{aeg::math::identity()};
    math::ClipSpaceConvention clipSpace{true, false};
    // Direct viewProjection assignments must also supply matching world-space camera vectors.
    aeg::math::Vec3 cameraPosition{};
    aeg::math::Vec3 cameraTowardViewer{0, 0, 1};
    void set_camera(const camera::RenderCamera& value) {
        math::Mat4 inverseView, inverseProjection;
        const auto candidate = value.view_projection();
        math::Vec3 position, towardViewer;
        // Camera projection constructors already validate invertibility without a
        // world-scale-dependent conditioning cutoff; preserve their full zoom range.
        if (!math::is_finite(candidate) || !math::try_inverse(value.view, inverseView, 0) ||
            !math::try_inverse(value.projection, inverseProjection, 0) ||
            !math::try_transform_point(inverseView, {}, position) ||
            !math::try_transform_vector(inverseView, {0, 0, 1}, towardViewer) ||
            !math::try_normalize(towardViewer, towardViewer))
            throw std::invalid_argument("Invalid render camera");
        viewProjection = candidate;
        clipSpace = value.convention;
        cameraPosition = position;
        cameraTowardViewer = towardViewer;
    }
    std::array<Light, kMaxLights> lights{};
    std::uint32_t lightCount{1}; // Zero selects ambient-only lighting; excess entries are ignored.
    float ambient{0.28f};
    aeg::math::Vec4 background{0.035f, 0.045f, 0.065f, 1};
    bool unlit{false};
    bool normalMapping{true};
    float normalStrength{1}; // Global multiplier on material normalScale, clamped to [0, 4].
};
}
