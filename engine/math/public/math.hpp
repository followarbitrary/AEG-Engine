#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <type_traits>

namespace aeg::math {

// try_* functions compute transactionally: false preserves out, including aliases.
// Value wrappers return zero on failure, except Quat wrappers return unit Quat{}.
// Right-handed coordinates: +X right, +Y up, camera forward -Z. Angles are radians.
// Matrices contain columns; column vectors compose as P*V*M and T*R*S.

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kEpsilon = 1.0e-6f;
constexpr float to_radians(float degrees) noexcept { return degrees * (kPi / 180.0f); }
constexpr float to_degrees(float radians) noexcept { return radians * (180.0f / kPi); }

struct Vec4;
struct DVec3;
struct Vec2 {
    using value_type = float;
    static constexpr std::size_t component_count = 2;
    float x{}, y{};
    constexpr Vec2() noexcept = default;
    explicit constexpr Vec2(float s) noexcept : x(s), y(s) {}
    constexpr Vec2(float a, float b) noexcept : x(a), y(b) {}
    constexpr float& operator[](std::size_t i) noexcept {
        assert(i < 2);
        return i == 0 ? x : y;
    }
    constexpr const float& operator[](std::size_t i) const noexcept {
        assert(i < 2);
        return i == 0 ? x : y;
    }
};
// The final four bytes of Vec3 are nonsemantic padding. Never serialize it as packed xyz.
struct alignas(16) Vec3 {
    using value_type = float;
    static constexpr std::size_t component_count = 3;
    float x{}, y{}, z{};
    constexpr Vec3() noexcept = default;
    explicit constexpr Vec3(float s) noexcept : x(s), y(s), z(s) {}
    constexpr Vec3(float a, float b, float c) noexcept : x(a), y(b), z(c) {}
    explicit constexpr Vec3(const Vec4& v) noexcept;
    explicit constexpr Vec3(const DVec3& v) noexcept;
    constexpr float& operator[](std::size_t i) noexcept {
        assert(i < 3);
        return i == 0 ? x : i == 1 ? y : z;
    }
    constexpr const float& operator[](std::size_t i) const noexcept {
        assert(i < 3);
        return i == 0 ? x : i == 1 ? y : z;
    }
};
struct alignas(16) Vec4 {
    using value_type = float;
    static constexpr std::size_t component_count = 4;
    float x{}, y{}, z{}, w{};
    constexpr Vec4() noexcept = default;
    explicit constexpr Vec4(float s) noexcept : x(s), y(s), z(s), w(s) {}
    constexpr Vec4(float a, float b, float c, float d) noexcept : x(a), y(b), z(c), w(d) {}
    constexpr Vec4(Vec3 v, float d) noexcept : x(v.x), y(v.y), z(v.z), w(d) {}
    constexpr float& operator[](std::size_t i) noexcept {
        assert(i < 4);
        return i == 0 ? x : i == 1 ? y : i == 2 ? z : w;
    }
    constexpr const float& operator[](std::size_t i) const noexcept {
        assert(i < 4);
        return i == 0 ? x : i == 1 ? y : i == 2 ? z : w;
    }
};
struct DVec2 {
    using value_type = double;
    static constexpr std::size_t component_count = 2;
    double x{}, y{};
    constexpr DVec2() noexcept = default;
    explicit constexpr DVec2(double s) noexcept : x(s), y(s) {}
    constexpr DVec2(double a, double b) noexcept : x(a), y(b) {}
    explicit constexpr DVec2(Vec2 v) noexcept : x(v.x), y(v.y) {}
    constexpr double& operator[](std::size_t i) noexcept {
        assert(i < 2);
        return i == 0 ? x : y;
    }
    constexpr const double& operator[](std::size_t i) const noexcept {
        assert(i < 2);
        return i == 0 ? x : y;
    }
};
struct DVec3 {
    using value_type = double;
    static constexpr std::size_t component_count = 3;
    double x{}, y{}, z{};
    constexpr DVec3() noexcept = default;
    explicit constexpr DVec3(double s) noexcept : x(s), y(s), z(s) {}
    constexpr DVec3(double a, double b, double c) noexcept : x(a), y(b), z(c) {}
    explicit constexpr DVec3(Vec3 v) noexcept : x(v.x), y(v.y), z(v.z) {}
    constexpr double& operator[](std::size_t i) noexcept {
        assert(i < 3);
        return i == 0 ? x : i == 1 ? y : z;
    }
    constexpr const double& operator[](std::size_t i) const noexcept {
        assert(i < 3);
        return i == 0 ? x : i == 1 ? y : z;
    }
};
constexpr Vec3::Vec3(const Vec4& v) noexcept : x(v.x), y(v.y), z(v.z) {}
constexpr Vec3::Vec3(const DVec3& v) noexcept
    : x(static_cast<float>(v.x)), y(static_cast<float>(v.y)), z(static_cast<float>(v.z)) {}

// Component arithmetic uses floating-point operations. Division, min and max
// produce a NaN lane if either operand of that lane is nonfinite.
#define AEG_MATH_VECTOR_API(V, S)                                                                                      \
    V operator+(V a, V b) noexcept;                                                                                    \
    V operator-(V a, V b) noexcept;                                                                                    \
    V operator-(V a) noexcept;                                                                                         \
    V operator*(V a, V b) noexcept;                                                                                    \
    V operator/(V a, V b) noexcept;                                                                                    \
    V operator*(V a, S s) noexcept;                                                                                    \
    V operator*(S s, V a) noexcept;                                                                                    \
    V operator/(V a, S s) noexcept;                                                                                    \
    V& operator+=(V& a, V b) noexcept;                                                                                 \
    V& operator-=(V& a, V b) noexcept;                                                                                 \
    V& operator*=(V& a, S s) noexcept;                                                                                 \
    V& operator/=(V& a, S s) noexcept;                                                                                 \
    bool operator==(V a, V b) noexcept;                                                                                \
    bool operator!=(V a, V b) noexcept;                                                                                \
    S dot(V a, V b) noexcept;                                                                                          \
    S length_squared(V v) noexcept;                                                                                    \
    S length(V v) noexcept;                                                                                            \
    V min(V a, V b) noexcept;                                                                                          \
    V max(V a, V b) noexcept;                                                                                          \
    bool is_finite(V v) noexcept;                                                                                      \
    bool nearly_equal(V a, V b, S absolute_tolerance) noexcept;                                                        \
    bool try_normalize(V v, V& out) noexcept;                                                                          \
    V normalize(V v) noexcept;
AEG_MATH_VECTOR_API(Vec2, float)
AEG_MATH_VECTOR_API(Vec3, float)
AEG_MATH_VECTOR_API(Vec4, float)
AEG_MATH_VECTOR_API(DVec2, double)
AEG_MATH_VECTOR_API(DVec3, double)
#undef AEG_MATH_VECTOR_API
Vec3 cross(Vec3 a, Vec3 b) noexcept;
DVec3 cross(DVec3 a, DVec3 b) noexcept;

struct alignas(16) Quat {
    float x{}, y{}, z{}, w{1.0f};
    constexpr Quat() noexcept = default;
    constexpr Quat(float a, float b, float c, float d) noexcept : x(a), y(b), z(c), w(d) {}
};
bool operator==(Quat a, Quat b) noexcept;
bool operator!=(Quat a, Quat b) noexcept;
bool is_finite(Quat q) noexcept;
bool nearly_equal(Quat a, Quat b, float absolute_tolerance) noexcept;
bool is_unit(Quat q) noexcept;
Quat conjugate(Quat q) noexcept;
Quat operator*(Quat a, Quat b) noexcept;
bool try_normalize(Quat q, Quat& out) noexcept;
Quat normalize(Quat q) noexcept;
bool try_inverse(Quat q, Quat& out) noexcept;
Quat inverse(Quat q) noexcept;
bool try_axis_angle(Vec3 axis, float radians, Quat& out) noexcept;
Quat axis_angle(Vec3 axis, float radians) noexcept;
// Rotation consumers require finite unit quaternions (norm squared within
// 8*kEpsilon of one); they never normalize silently.
bool try_rotate(Quat q, Vec3 v, Vec3& out) noexcept;
Vec3 rotate(Quat q, Vec3 v) noexcept;

struct alignas(16) Mat4 {
    Vec4 columns[4]{};
    constexpr Mat4() noexcept = default;
    constexpr Mat4(Vec4 a, Vec4 b, Vec4 c, Vec4 d) noexcept : columns{a, b, c, d} {}
    constexpr Vec4& operator[](std::size_t column) noexcept {
        assert(column < 4);
        return columns[column];
    }
    constexpr const Vec4& operator[](std::size_t column) const noexcept {
        assert(column < 4);
        return columns[column];
    }
    constexpr float& at(std::size_t row, std::size_t column) noexcept { return (*this)[column][row]; }
    constexpr const float& at(std::size_t row, std::size_t column) const noexcept { return (*this)[column][row]; }
};
Mat4 identity() noexcept;
bool operator==(const Mat4& a, const Mat4& b) noexcept;
bool operator!=(const Mat4& a, const Mat4& b) noexcept;
bool is_finite(const Mat4& m) noexcept;
bool nearly_equal(const Mat4& a, const Mat4& b, float absolute_tolerance) noexcept;
Mat4 operator*(const Mat4& a, const Mat4& b) noexcept;
Vec4 operator*(const Mat4& m, Vec4 v) noexcept;
Mat4 transpose(const Mat4& m) noexcept;
// Row-scaled pivots and reciprocal infinity-norm condition estimate must exceed
// relative_tolerance. Affine matrices test only the 3x3 linear part, so uniform
// tiny scale and large translation remain valid. The result must be finite.
// Even at tolerance zero, a determinant cancellation guard rejects numerical
// rank loss: abs(det) must exceed 64*double-epsilon*sum(abs(det terms)).
bool try_inverse(const Mat4& m, Mat4& out, float relative_tolerance = kEpsilon) noexcept;
Mat4 inverse(const Mat4& m, float relative_tolerance = kEpsilon) noexcept;
bool try_translation(Vec3 v, Mat4& out) noexcept;
Mat4 translation(Vec3 v) noexcept;
bool try_scale(Vec3 v, Mat4& out) noexcept;
Mat4 scale(Vec3 v) noexcept;
bool try_rotation(Vec3 axis, float radians, Mat4& out) noexcept;
Mat4 rotation(Vec3 axis, float radians) noexcept;
bool try_from_quat(Quat q, Mat4& out) noexcept;
Mat4 from_quat(Quat q) noexcept;
bool try_trs(Vec3 t, Quat r, Vec3 s, Mat4& out) noexcept;
Mat4 trs(Vec3 t, Quat r, Vec3 s) noexcept;
bool try_look_at(Vec3 eye, Vec3 target, Vec3 up, Mat4& out) noexcept;
Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) noexcept;
// Transform/projection/frustum checks require finite input and double-precision
// invertibility with the relative determinant rank guard above and no world-scale
// threshold; they do not require a float inverse.
// Points use w=1 with perspective divide (nonzero w); vectors use w=0 without divide.
bool try_transform_point(const Mat4& m, Vec3 p, Vec3& out) noexcept;
Vec3 transform_point(const Mat4& m, Vec3 p) noexcept;
bool try_transform_vector(const Mat4& m, Vec3 v, Vec3& out) noexcept;
Vec3 transform_vector(const Mat4& m, Vec3 v) noexcept;

struct ClipSpaceConvention {
    bool y_down{false};
    bool reversed_z{false};
    constexpr bool operator==(const ClipSpaceConvention&) const noexcept = default;
};
bool try_perspective(float vertical_fov_radians, float aspect, float near_distance, float far_distance, Mat4& out,
                     ClipSpaceConvention convention = {}) noexcept;
Mat4 perspective(float vertical_fov_radians, float aspect, float near_distance, float far_distance,
                 ClipSpaceConvention convention = {}) noexcept;
bool try_orthographic(float left, float right, float bottom, float top, float near_distance, float far_distance,
                      Mat4& out, ClipSpaceConvention convention = {}) noexcept;
Mat4 orthographic(float left, float right, float bottom, float top, float near_distance, float far_distance,
                  ClipSpaceConvention convention = {}) noexcept;

struct Plane {
    Vec3 normal{};
    float offset{};
};
// Plane equation normal dot point + offset >= 0 means inside. Classification
// requires finite unit normals. Extraction labels physical planes for both flags.
enum FrustumPlane : std::size_t { kLeft, kRight, kBottom, kTop, kNear, kFar };
struct Frustum {
    std::array<Plane, 6> planes{};
    bool contains_point(Vec3 point) const noexcept;
    bool intersects_sphere(Vec3 center, float radius) const noexcept;
    bool intersects_aabb(Vec3 minimum, Vec3 maximum) const noexcept;
};
bool is_finite(const Plane& plane) noexcept;
bool is_valid(const Plane& plane) noexcept;
bool is_valid(const Frustum& frustum) noexcept;
bool try_normalize(const Plane& plane, Plane& out) noexcept;
Plane normalize(const Plane& plane) noexcept;
float signed_distance(const Plane& plane, Vec3 point) noexcept;
bool try_extract_frustum(const Mat4& view_projection, Frustum& out, ClipSpaceConvention convention = {}) noexcept;
Frustum extract_frustum(const Mat4& view_projection, ClipSpaceConvention convention = {}) noexcept;
bool contains_point(const Frustum& frustum, Vec3 point) noexcept;
bool intersects_sphere(const Frustum& frustum, Vec3 center, float radius) noexcept;
bool intersects_aabb(const Frustum& frustum, Vec3 minimum, Vec3 maximum) noexcept;

static_assert(sizeof(Vec2) == 8 && alignof(Vec2) == alignof(float));
static_assert(sizeof(Vec3) == 16 && alignof(Vec3) == 16);
static_assert(offsetof(Vec3, x) == 0 && offsetof(Vec3, y) == 4 && offsetof(Vec3, z) == 8);
static_assert(sizeof(Vec4) == 16 && alignof(Vec4) == 16);
static_assert(sizeof(Quat) == 16 && alignof(Quat) == 16);
static_assert(sizeof(Mat4) == 64 && alignof(Mat4) == 16);
static_assert(std::is_standard_layout_v<Vec2> && std::is_trivially_copyable_v<Vec2>);
static_assert(std::is_standard_layout_v<Vec3> && std::is_trivially_copyable_v<Vec3>);
static_assert(std::is_standard_layout_v<Vec4> && std::is_trivially_copyable_v<Vec4>);
static_assert(std::is_standard_layout_v<Quat> && std::is_trivially_copyable_v<Quat>);
static_assert(std::is_standard_layout_v<Mat4> && std::is_trivially_copyable_v<Mat4>);

} // namespace aeg::math
