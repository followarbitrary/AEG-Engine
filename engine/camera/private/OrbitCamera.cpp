#include <OrbitCamera.hpp>
#include <math.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace aeg::camera {
namespace {
constexpr float fieldOfView = 0.78539816339f;
constexpr float halfPi = std::numbers::pi_v<float> * 0.5f;
constexpr float maximumSafeRadius = std::numeric_limits<float>::max() / 16.0f;
constexpr float maximumSafeDistance = std::numeric_limits<float>::max() / 8.0f;
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
struct OrbitBasis {
    math::Vec3 backward;
    math::Vec3 right;
    math::Vec3 up;
};
OrbitBasis orbitBasis(float yaw, float pitch) {
    const float sinYaw = std::sin(yaw), cosYaw = std::cos(yaw);
    // A float approximation of pi/2 has a small negative cosine. Exact poles
    // need a true zero to avoid tilting and flipping the apparent up direction.
    const bool pole = std::abs(pitch) == halfPi;
    const float sinPitch = pole ? std::copysign(1.0f, pitch) : std::sin(pitch);
    const float cosPitch = pole ? 0.0f : std::cos(pitch);
    return {{cosPitch * sinYaw, sinPitch, cosPitch * cosYaw},
            {cosYaw, 0, -sinYaw},
            {-sinPitch * sinYaw, cosPitch, -sinPitch * cosYaw}};
}
}
void OrbitCamera::fit(const foundation::Bounds& bounds) {
    if (!finite(bounds.min) || !finite(bounds.max)) return;
    // Difference-first avoids overflow for models far from the origin.
    target_ = bounds.min + (bounds.max - bounds.min) * 0.5f;
    // Keep the derived clip range representable. The importer accepts finite
    // float coordinates, but a finite model radius can still overflow after
    // the fit distance and far-plane multipliers are applied.
    radius_ = std::min(bounds.radius(), maximumSafeRadius);
    if (!finite(target_) || !std::isfinite(radius_)) {
        target_ = {0, 0, 0};
        radius_ = 1;
    }
    distance_ = std::min(radius_ / std::sin(fieldOfView * 0.5f) * 1.2f, maximumSafeDistance);
    if (!finite(position())) {
        target_ = {0, 0, 0};
        radius_ = 1;
        distance_ = 1.0f / std::sin(fieldOfView * 0.5f) * 1.2f;
    }
}
bool OrbitCamera::setViewDirection(math::Vec3 towardViewer) {
    if (!finite(towardViewer)) return false;
    // atan2/hypot in double support all nonzero finite float magnitudes,
    // including subnormals, without overflow or a world-scale epsilon.
    const double horizontal = std::hypot(static_cast<double>(towardViewer.x), static_cast<double>(towardViewer.z));
    if (horizontal == 0 && towardViewer.y == 0) return false;
    const float yaw =
        horizontal == 0
            ? 0.0f
            : static_cast<float>(std::atan2(static_cast<double>(towardViewer.x), static_cast<double>(towardViewer.z)));
    const float pitch = static_cast<float>(std::atan2(static_cast<double>(towardViewer.y), horizontal));
    yaw_ = yaw;
    pitch_ = pitch;
    return true;
}
void OrbitCamera::orbit(float deltaX, float deltaY) {
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return;
    // Move the camera opposite the drag so the visible model surface follows it.
    yaw_ = std::remainder(yaw_ - std::remainder(deltaX, 1256.637f) * 0.005f, 2 * std::numbers::pi_v<float>);
    pitch_ = std::clamp(pitch_ + deltaY * 0.005f, -halfPi, halfPi);
}
void OrbitCamera::pan(float deltaX, float deltaY, float viewportHeight) {
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY) || !std::isfinite(viewportHeight) || viewportHeight <= 0)
        return;
    const auto basis = orbitBasis(yaw_, pitch_);
    const auto offset =
        (-deltaX * basis.right + deltaY * basis.up) * (2 * distance_ * std::tan(fieldOfView * 0.5f) / viewportHeight);
    if (finite(offset) && finite(target_ + offset)) target_ += offset;
}
void OrbitCamera::zoom(float steps) {
    if (!std::isfinite(steps)) return;
    const auto maximumDistance = std::min(radius_ * 1000.0f, maximumSafeDistance);
    distance_ = std::clamp(distance_ * std::exp(std::clamp(-steps * 0.15f, -30.0f, 30.0f)), radius_ * 0.02f,
                            maximumDistance);
}
math::Vec3 OrbitCamera::position() const { return target_ + distance_ * orbitBasis(yaw_, pitch_).backward; }
math::Mat4 OrbitCamera::view() const { return math::look_at(position(), target_, orbitBasis(yaw_, pitch_).up); }
math::Mat4 OrbitCamera::projection(float aspect, math::ClipSpaceConvention convention) const {
    if (!std::isfinite(aspect) || aspect <= 0) aspect = 1;
    aspect = std::clamp(aspect, 0.01f, 100.0f);
    // Keep the smaller viewport dimension at the fitted field of view.
    const float verticalFov = 2 * std::atan(std::tan(fieldOfView * 0.5f) / std::min(aspect, 1.0f));
    // A tiny radius-only near plane loses depth separation between skin and
    // thin overlays when zoomed in. Follow distance to retain precision while
    // still allowing close inspection all the way to the minimum zoom.
    const float nearPlane = std::max({radius_ * 0.0001f, distance_ * 0.01f, distance_ - radius_ * 1.5f});
    const float farPlane = std::max(nearPlane * 2, distance_ + radius_ * 4);
    return math::perspective(verticalFov, aspect, nearPlane, farPlane, convention);
}
RenderCamera OrbitCamera::render_camera(float aspect, math::ClipSpaceConvention convention) const {
    return {view(), projection(aspect, convention), convention};
}
}
