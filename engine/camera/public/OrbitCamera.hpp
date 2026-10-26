#pragma once
#include <Bounds.hpp>
#include <RenderCamera.hpp>

namespace aeg::camera {
class OrbitCamera {
public:
    // Reframe the bounds without changing the current orbit angle.
    void fit(const foundation::Bounds& bounds);
    // Mouse delta in pixels (+X right, +Y down); horizontal surface motion follows the drag.
    void orbit(float deltaX, float deltaY);
    void pan(float deltaX, float deltaY, float viewportHeight);
    void zoom(float steps);
    [[nodiscard]] math::Mat4 view() const;
    [[nodiscard]] math::Mat4 projection(float aspect, math::ClipSpaceConvention convention = {}) const;
    [[nodiscard]] RenderCamera render_camera(float aspect, math::ClipSpaceConvention convention = {}) const;
    [[nodiscard]] math::Vec3 position() const;
    [[nodiscard]] math::Vec3 target() const { return target_; }
    [[nodiscard]] float distance() const { return distance_; }

private:
    math::Vec3 target_{0};
    float yaw_{0}, pitch_{0}, distance_{5}, radius_{1};
};
}
