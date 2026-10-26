#include "math.hpp"
#include "matrix_validation.hpp"
#include <cmath>

namespace aeg::math {
bool try_perspective(float fov, float aspect, float near_distance, float far_distance, Mat4& out,
                     ClipSpaceConvention convention) noexcept {
    if (!std::isfinite(fov) || !std::isfinite(aspect) || !std::isfinite(near_distance) ||
        !std::isfinite(far_distance) || fov <= 0 || fov >= kPi || aspect <= 0 || near_distance <= 0 ||
        far_distance <= near_distance)
        return false;
    const double n = near_distance, f = far_distance, reciprocal_tan = 1.0 / std::tan(static_cast<double>(fov) * .5);
    Mat4 r;
    r.at(0, 0) = static_cast<float>(reciprocal_tan / aspect);
    r.at(1, 1) = static_cast<float>(convention.y_down ? -reciprocal_tan : reciprocal_tan);
    r.at(2, 2) = static_cast<float>(convention.reversed_z ? n / (f - n) : -f / (f - n));
    r.at(2, 3) = static_cast<float>(convention.reversed_z ? f * n / (f - n) : -f * n / (f - n));
    r.at(3, 2) = -1;
    if (!detail::is_invertible(r)) return false;
    out = r;
    return true;
}
Mat4 perspective(float fov, float aspect, float n, float f, ClipSpaceConvention convention) noexcept {
    Mat4 r;
    (void)try_perspective(fov, aspect, n, f, r, convention);
    return r;
}
bool try_orthographic(float left, float right, float bottom, float top, float near_distance, float far_distance,
                      Mat4& out, ClipSpaceConvention convention) noexcept {
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(bottom) || !std::isfinite(top) ||
        !std::isfinite(near_distance) || !std::isfinite(far_distance) || right <= left || top <= bottom ||
        near_distance <= 0 || far_distance <= near_distance)
        return false;
    const double l = left, rgt = right, b = bottom, t = top, n = near_distance, f = far_distance;
    const double y_sign = convention.y_down ? -1.0 : 1.0;
    Mat4 r = identity();
    r.at(0, 0) = static_cast<float>(2 / (rgt - l));
    r.at(0, 3) = static_cast<float>(-(rgt + l) / (rgt - l));
    r.at(1, 1) = static_cast<float>(y_sign * 2 / (t - b));
    r.at(1, 3) = static_cast<float>(-y_sign * (t + b) / (t - b));
    r.at(2, 2) = static_cast<float>(convention.reversed_z ? 1 / (f - n) : -1 / (f - n));
    r.at(2, 3) = static_cast<float>(convention.reversed_z ? f / (f - n) : -n / (f - n));
    if (!detail::is_invertible(r)) return false;
    out = r;
    return true;
}
Mat4 orthographic(float l, float r, float b, float t, float n, float f, ClipSpaceConvention convention) noexcept {
    Mat4 result;
    (void)try_orthographic(l, r, b, t, n, f, result, convention);
    return result;
}
} // namespace aeg::math
