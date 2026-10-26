#include "math.hpp"
#include "matrix_validation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace aeg::math {
namespace {
bool normalize_coefficients(double x, double y, double z, double offset, Plane& out) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(offset)) return false;
    const double magnitude = std::max({std::abs(x), std::abs(y), std::abs(z)});
    if (magnitude == 0) return false;
    const double sx = x / magnitude, sy = y / magnitude, sz = z / magnitude;
    const double length = magnitude * std::sqrt(sx * sx + sy * sy + sz * sz);
    const Plane result{
        Vec3(static_cast<float>(x / length), static_cast<float>(y / length), static_cast<float>(z / length)),
        static_cast<float>(offset / length)};
    if (!is_valid(result)) return false;
    out = result;
    return true;
}
double plane_distance(const Plane& p, Vec3 point) noexcept {
    return static_cast<double>(p.normal.x) * point.x + static_cast<double>(p.normal.y) * point.y +
           static_cast<double>(p.normal.z) * point.z + p.offset;
}
}
bool is_finite(const Plane& p) noexcept { return is_finite(p.normal) && std::isfinite(p.offset); }
bool is_valid(const Plane& p) noexcept {
    if (!is_finite(p)) return false;
    const double n = static_cast<double>(p.normal.x) * p.normal.x + static_cast<double>(p.normal.y) * p.normal.y +
                     static_cast<double>(p.normal.z) * p.normal.z;
    return std::abs(n - 1.0) <= 8.0 * kEpsilon;
}
bool is_valid(const Frustum& f) noexcept {
    for (const auto& p : f.planes)
        if (!is_valid(p)) return false;
    return true;
}
bool try_normalize(const Plane& p, Plane& out) noexcept {
    return normalize_coefficients(p.normal.x, p.normal.y, p.normal.z, p.offset, out);
}
Plane normalize(const Plane& p) noexcept {
    Plane r;
    (void)try_normalize(p, r);
    return r;
}
float signed_distance(const Plane& p, Vec3 point) noexcept {
    if (!is_valid(p) || !is_finite(point)) return std::numeric_limits<float>::quiet_NaN();
    return static_cast<float>(plane_distance(p, point));
}
bool try_extract_frustum(const Mat4& vp, Frustum& out, ClipSpaceConvention convention) noexcept {
    if (!detail::is_invertible(vp)) return false;
    double rows[4][4]{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t col = 0; col < 4; ++col) rows[row][col] = vp.at(row, col);
    Frustum r;
    const auto combination = [&](std::size_t target, std::size_t row, double sign) {
        return normalize_coefficients(rows[3][0] + sign * rows[row][0], rows[3][1] + sign * rows[row][1],
                                      rows[3][2] + sign * rows[row][2], rows[3][3] + sign * rows[row][3],
                                      r.planes[target]);
    };
    if (!combination(kLeft, 0, 1) || !combination(kRight, 0, -1) || !combination(kBottom, 1, 1) ||
        !combination(kTop, 1, -1) ||
        !normalize_coefficients(rows[2][0], rows[2][1], rows[2][2], rows[2][3], r.planes[kNear]) ||
        !combination(kFar, 2, -1))
        return false;
    if (convention.y_down) std::swap(r.planes[kBottom], r.planes[kTop]);
    if (convention.reversed_z) std::swap(r.planes[kNear], r.planes[kFar]);
    out = r;
    return true;
}
Frustum extract_frustum(const Mat4& vp, ClipSpaceConvention convention) noexcept {
    Frustum r;
    (void)try_extract_frustum(vp, r, convention);
    return r;
}
bool contains_point(const Frustum& f, Vec3 point) noexcept {
    if (!is_valid(f) || !is_finite(point)) return false;
    for (const auto& p : f.planes)
        if (plane_distance(p, point) < 0) return false;
    return true;
}
bool intersects_sphere(const Frustum& f, Vec3 center, float radius) noexcept {
    if (!is_valid(f) || !is_finite(center) || !std::isfinite(radius) || radius < 0) return false;
    for (const auto& p : f.planes)
        if (plane_distance(p, center) < -static_cast<double>(radius)) return false;
    return true;
}
bool intersects_aabb(const Frustum& f, Vec3 minimum, Vec3 maximum) noexcept {
    if (!is_valid(f) || !is_finite(minimum) || !is_finite(maximum) || minimum.x > maximum.x || minimum.y > maximum.y ||
        minimum.z > maximum.z)
        return false;
    for (const auto& p : f.planes) {
        const Vec3 positive{p.normal.x >= 0 ? maximum.x : minimum.x, p.normal.y >= 0 ? maximum.y : minimum.y,
                            p.normal.z >= 0 ? maximum.z : minimum.z};
        if (plane_distance(p, positive) < 0) return false;
    }
    return true;
}
bool Frustum::contains_point(Vec3 p) const noexcept { return math::contains_point(*this, p); }
bool Frustum::intersects_sphere(Vec3 p, float radius) const noexcept {
    return math::intersects_sphere(*this, p, radius);
}
bool Frustum::intersects_aabb(Vec3 minimum, Vec3 maximum) const noexcept {
    return math::intersects_aabb(*this, minimum, maximum);
}
} // namespace aeg::math
