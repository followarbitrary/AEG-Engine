#include "math.hpp"
#include <algorithm>
#include <cmath>

namespace aeg::math {
bool operator==(Quat a, Quat b) noexcept { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
bool operator!=(Quat a, Quat b) noexcept { return !(a == b); }
bool is_finite(Quat q) noexcept { return is_finite(Vec4(q.x, q.y, q.z, q.w)); }
bool nearly_equal(Quat a, Quat b, float tolerance) noexcept {
    return nearly_equal(Vec4(a.x, a.y, a.z, a.w), Vec4(b.x, b.y, b.z, b.w), tolerance);
}
bool is_unit(Quat q) noexcept {
    if (!is_finite(q)) return false;
    const double sum = static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
                       static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w;
    return std::abs(sum - 1.0) <= 8.0 * kEpsilon;
}
Quat conjugate(Quat q) noexcept { return {-q.x, -q.y, -q.z, q.w}; }
Quat operator*(Quat a, Quat b) noexcept {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
bool try_normalize(Quat q, Quat& out) noexcept {
    Vec4 v;
    if (!try_normalize(Vec4(q.x, q.y, q.z, q.w), v)) return false;
    out = {v.x, v.y, v.z, v.w};
    return true;
}
Quat normalize(Quat q) noexcept {
    Quat r;
    (void)try_normalize(q, r);
    return r;
}
bool try_inverse(Quat q, Quat& out) noexcept {
    if (!is_finite(q)) return false;
    const double maximum = std::max({std::abs(static_cast<double>(q.x)), std::abs(static_cast<double>(q.y)),
                                     std::abs(static_cast<double>(q.z)), std::abs(static_cast<double>(q.w))});
    if (maximum == 0) return false;
    const double x = q.x / maximum, y = q.y / maximum, z = q.z / maximum, w = q.w / maximum;
    const double denominator = maximum * (x * x + y * y + z * z + w * w);
    const Quat r{static_cast<float>(-x / denominator), static_cast<float>(-y / denominator),
                 static_cast<float>(-z / denominator), static_cast<float>(w / denominator)};
    if (!is_finite(r)) return false;
    out = r;
    return true;
}
Quat inverse(Quat q) noexcept {
    Quat r;
    (void)try_inverse(q, r);
    return r;
}
bool try_axis_angle(Vec3 axis, float radians, Quat& out) noexcept {
    Vec3 n;
    if (!std::isfinite(radians) || !try_normalize(axis, n)) return false;
    const double half = static_cast<double>(radians) * .5, s = std::sin(half);
    const Quat r{static_cast<float>(n.x * s), static_cast<float>(n.y * s), static_cast<float>(n.z * s),
                 static_cast<float>(std::cos(half))};
    if (!is_unit(r)) return false;
    out = r;
    return true;
}
Quat axis_angle(Vec3 axis, float radians) noexcept {
    Quat r;
    (void)try_axis_angle(axis, radians, r);
    return r;
}
bool try_rotate(Quat q, Vec3 v, Vec3& out) noexcept {
    if (!is_unit(q) || !is_finite(v)) return false;
    const double tx = 2.0 * (static_cast<double>(q.y) * v.z - static_cast<double>(q.z) * v.y);
    const double ty = 2.0 * (static_cast<double>(q.z) * v.x - static_cast<double>(q.x) * v.z);
    const double tz = 2.0 * (static_cast<double>(q.x) * v.y - static_cast<double>(q.y) * v.x);
    const Vec3 r{static_cast<float>(v.x + q.w * tx + q.y * tz - q.z * ty),
                 static_cast<float>(v.y + q.w * ty + q.z * tx - q.x * tz),
                 static_cast<float>(v.z + q.w * tz + q.x * ty - q.y * tx)};
    if (!is_finite(r)) return false;
    out = r;
    return true;
}
Vec3 rotate(Quat q, Vec3 v) noexcept {
    Vec3 r;
    (void)try_rotate(q, v, r);
    return r;
}
} // namespace aeg::math
