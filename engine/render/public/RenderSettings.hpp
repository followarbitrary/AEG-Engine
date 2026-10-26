#pragma once
#include <math.hpp>
#include <RenderCamera.hpp>

namespace aeg::render {
struct RenderSettings {
    aeg::math::Mat4 model{aeg::math::identity()};
    aeg::math::Mat4 viewProjection{aeg::math::identity()};
    math::ClipSpaceConvention clipSpace{true, false};
    void set_camera(const camera::RenderCamera& value) {
        viewProjection = value.view_projection();
        clipSpace = value.convention;
    }
    aeg::math::Vec3 lightDirection{-0.5f, -1.0f, -0.4f};
    float ambient{0.28f};
    aeg::math::Vec4 background{0.035f, 0.045f, 0.065f, 1};
    bool unlit{false};
    bool normalMapping{true};
    float normalStrength{1}; // Global multiplier on material normalScale, clamped to [0, 4].
};
}
