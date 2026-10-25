#pragma once
#include <Bounds.hpp>
#include <glm/glm.hpp>

namespace aeg::camera {
class OrbitCamera {
public:
    void fit(const foundation::Bounds& bounds);
    // Mouse delta in pixels (+X right, +Y down); horizontal surface motion follows the drag.
    void orbit(float deltaX, float deltaY);
    void pan(float deltaX, float deltaY, float viewportHeight);
    void zoom(float steps);
    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection(float aspect) const;
    [[nodiscard]] glm::vec3 position() const;
    [[nodiscard]] glm::vec3 target() const { return target_; }
    [[nodiscard]] float distance() const { return distance_; }

private:
    glm::vec3 target_{0};
    float yaw_{0.65f}, pitch_{0.35f}, distance_{5}, radius_{1};
};
}
