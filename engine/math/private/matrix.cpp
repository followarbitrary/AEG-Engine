#include "math.hpp"
#include "simd.hpp"
#include "matrix_validation.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace aeg::math {
Mat4 identity() noexcept { return {Vec4(1, 0, 0, 0), Vec4(0, 1, 0, 0), Vec4(0, 0, 1, 0), Vec4(0, 0, 0, 1)}; }
bool operator==(const Mat4& a, const Mat4& b) noexcept {
    for (std::size_t c = 0; c < 4; ++c)
        if (a[c] != b[c]) return false;
    return true;
}
bool operator!=(const Mat4& a, const Mat4& b) noexcept { return !(a == b); }
bool is_finite(const Mat4& m) noexcept {
    for (std::size_t c = 0; c < 4; ++c)
        if (!is_finite(m[c])) return false;
    return true;
}
bool nearly_equal(const Mat4& a, const Mat4& b, float tolerance) noexcept {
    for (std::size_t c = 0; c < 4; ++c)
        if (!nearly_equal(a[c], b[c], tolerance)) return false;
    return true;
}
Mat4 operator*(const Mat4& a, const Mat4& b) noexcept { return detail::multiply(a, b); }
Vec4 operator*(const Mat4& m, Vec4 v) noexcept { return detail::multiply(m, v); }
Mat4 transpose(const Mat4& m) noexcept {
    Mat4 r;
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t row = 0; row < 4; ++row) r.at(row, c) = m.at(c, row);
    return r;
}

bool try_translation(Vec3 v, Mat4& out) noexcept {
    if (!is_finite(v)) return false;
    Mat4 r = identity();
    r[3] = Vec4(v, 1);
    out = r;
    return true;
}
Mat4 translation(Vec3 v) noexcept {
    Mat4 r;
    (void)try_translation(v, r);
    return r;
}
bool try_scale(Vec3 v, Mat4& out) noexcept {
    if (!is_finite(v) || v.x == 0 || v.y == 0 || v.z == 0) return false;
    Mat4 r = identity();
    r.at(0, 0) = v.x;
    r.at(1, 1) = v.y;
    r.at(2, 2) = v.z;
    out = r;
    return true;
}
Mat4 scale(Vec3 v) noexcept {
    Mat4 r;
    (void)try_scale(v, r);
    return r;
}
bool try_from_quat(Quat q, Mat4& out) noexcept {
    if (!is_unit(q)) return false;
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 r = identity();
    r.at(0, 0) = static_cast<float>(1 - 2 * (y * y + z * z));
    r.at(0, 1) = static_cast<float>(2 * (x * y - z * w));
    r.at(0, 2) = static_cast<float>(2 * (x * z + y * w));
    r.at(1, 0) = static_cast<float>(2 * (x * y + z * w));
    r.at(1, 1) = static_cast<float>(1 - 2 * (x * x + z * z));
    r.at(1, 2) = static_cast<float>(2 * (y * z - x * w));
    r.at(2, 0) = static_cast<float>(2 * (x * z - y * w));
    r.at(2, 1) = static_cast<float>(2 * (y * z + x * w));
    r.at(2, 2) = static_cast<float>(1 - 2 * (x * x + y * y));
    out = r;
    return true;
}
Mat4 from_quat(Quat q) noexcept {
    Mat4 r;
    (void)try_from_quat(q, r);
    return r;
}
bool try_rotation(Vec3 axis, float radians, Mat4& out) noexcept {
    Quat q;
    Mat4 r;
    if (!try_axis_angle(axis, radians, q) || !try_from_quat(q, r)) return false;
    out = r;
    return true;
}
Mat4 rotation(Vec3 axis, float radians) noexcept {
    Mat4 r;
    (void)try_rotation(axis, radians, r);
    return r;
}
bool try_trs(Vec3 t, Quat q, Vec3 s, Mat4& out) noexcept {
    Mat4 tm, rm, sm;
    if (!try_translation(t, tm) || !try_from_quat(q, rm) || !try_scale(s, sm)) return false;
    const Mat4 r = tm * rm * sm;
    if (!is_finite(r)) return false;
    out = r;
    return true;
}
Mat4 trs(Vec3 t, Quat q, Vec3 s) noexcept {
    Mat4 r;
    (void)try_trs(t, q, s, r);
    return r;
}
bool try_look_at(Vec3 eye, Vec3 target, Vec3 up, Mat4& out) noexcept {
    if (!is_finite(eye) || !is_finite(target) || !is_finite(up)) return false;
    DVec3 forward, right, up_unit;
    if (!try_normalize(DVec3(target) - DVec3(eye), forward) || !try_normalize(DVec3(up), up_unit) ||
        !try_normalize(cross(forward, up_unit), right))
        return false;
    const DVec3 vertical = cross(right, forward), deye(eye);
    Mat4 r = identity();
    for (std::size_t i = 0; i < 3; ++i) {
        r.at(0, i) = static_cast<float>(right[i]);
        r.at(1, i) = static_cast<float>(vertical[i]);
        r.at(2, i) = static_cast<float>(-forward[i]);
    }
    r.at(0, 3) = static_cast<float>(-dot(right, deye));
    r.at(1, 3) = static_cast<float>(-dot(vertical, deye));
    r.at(2, 3) = static_cast<float>(dot(forward, deye));
    if (!is_finite(r)) return false;
    out = r;
    return true;
}
Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) noexcept {
    Mat4 r;
    (void)try_look_at(eye, target, up, r);
    return r;
}
namespace {
bool valid_transform(const Mat4& m) noexcept { return detail::is_invertible(m); }
}
bool try_transform_point(const Mat4& m, Vec3 p, Vec3& out) noexcept {
    if (!is_finite(p) || !valid_transform(m)) return false;
    const Vec4 homogeneous = m * Vec4(p, 1);
    if (!is_finite(homogeneous) || homogeneous.w == 0) return false;
    const Vec3 r{homogeneous.x / homogeneous.w, homogeneous.y / homogeneous.w, homogeneous.z / homogeneous.w};
    if (!is_finite(r)) return false;
    out = r;
    return true;
}
Vec3 transform_point(const Mat4& m, Vec3 p) noexcept {
    Vec3 r;
    (void)try_transform_point(m, p, r);
    return r;
}
bool try_transform_vector(const Mat4& m, Vec3 v, Vec3& out) noexcept {
    if (!is_finite(v) || !valid_transform(m)) return false;
    const Vec4 homogeneous = m * Vec4(v, 0);
    if (!is_finite(homogeneous)) return false;
    const Vec3 r(homogeneous);
    out = r;
    return true;
}
Vec3 transform_vector(const Mat4& m, Vec3 v) noexcept {
    Vec3 r;
    (void)try_transform_vector(m, v, r);
    return r;
}
} // namespace aeg::math
