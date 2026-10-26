#include <OrbitCamera.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace aeg::camera {
namespace {
constexpr float fieldOfView = 0.78539816339f;
bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}
void OrbitCamera::fit(const foundation::Bounds& bounds) {
    if (!finite(bounds.min) || !finite(bounds.max)) return;
    // Difference-first avoids overflow for models far from the origin.
    target_ = bounds.min + (bounds.max - bounds.min) * 0.5f;
    radius_ = bounds.radius();
    if (!finite(target_) || !std::isfinite(radius_)) {
        target_ = {0, 0, 0};
        radius_ = 1;
    }
    distance_ = radius_ / std::sin(fieldOfView * 0.5f) * 1.2f;
}
void OrbitCamera::orbit(float deltaX, float deltaY) {
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return;
    // Move the camera opposite the drag so the visible model surface follows it.
    yaw_ = std::remainder(yaw_ - std::remainder(deltaX, 1256.637f) * 0.005f, 2 * std::numbers::pi_v<float>);
    pitch_ = std::clamp(pitch_ + deltaY * 0.005f, -1.55334f, 1.55334f);
}
void OrbitCamera::pan(float deltaX, float deltaY, float viewportHeight) {
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !std::isfinite(viewportHeight) || viewportHeight <= 0)
        return;
    const auto forward = glm::normalize(target_ - position());
    const auto right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    const auto up = glm::normalize(glm::cross(right, forward));
    const auto offset =
        (-deltaX * right + deltaY * up) * (2 * distance_ * std::tan(fieldOfView * 0.5f) / viewportHeight);
    if (finite(offset) && finite(target_ + offset)) target_ += offset;
}
void OrbitCamera::zoom(float steps) {
    if (!std::isfinite(steps)) return;
    distance_ =
        std::clamp(distance_ * std::exp(std::clamp(-steps * 0.15f, -30.0f, 30.0f)), radius_ * 0.02f, radius_ * 1000.0f);
}
glm::vec3 OrbitCamera::position() const {
    return target_ + distance_ * glm::vec3(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_),
                                           std::cos(pitch_) * std::cos(yaw_));
}
glm::mat4 OrbitCamera::view() const { return glm::lookAt(position(), target_, glm::vec3(0, 1, 0)); }
glm::mat4 OrbitCamera::projection(float aspect) const {
    if (!std::isfinite(aspect) || aspect <= 0) aspect = 1;
    aspect = std::clamp(aspect, 0.01f, 100.0f);
    // Keep the smaller viewport dimension at the fitted field of view.
    const float verticalFov = 2 * std::atan(std::tan(fieldOfView * 0.5f) / std::min(aspect, 1.0f));
    // A tiny radius-only near plane loses depth separation between skin and
    // thin overlays when zoomed in. Follow distance to retain precision while
    // still allowing close inspection all the way to the minimum zoom.
    const float nearPlane = std::max({radius_ * 0.0001f, distance_ * 0.01f, distance_ - radius_ * 1.5f});
    const float farPlane = std::max(nearPlane * 2, distance_ + radius_ * 4);
    auto result = glm::perspective(verticalFov, aspect, nearPlane, farPlane);
    result[1][1] *= -1;
    return result;
}
}
