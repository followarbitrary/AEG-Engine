#include "math.hpp"
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cfenv>
#include <iostream>
#include <limits>
#include <type_traits>
#include <array>
#include <cstdint>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#endif

using namespace aeg::math;
static_assert(sizeof(Vec2) == 8 && sizeof(Vec3) == 16 && alignof(Vec3) == 16);
static_assert(sizeof(Vec4) == 16 && alignof(Vec4) == 16 && sizeof(Quat) == 16 && alignof(Quat) == 16);
static_assert(sizeof(Mat4) == 64 && alignof(Mat4) == 16);
static_assert(offsetof(Vec3, x) == 0 && offsetof(Vec3, y) == 4 && offsetof(Vec3, z) == 8);
static_assert(std::is_standard_layout_v<Vec2> && std::is_trivially_copyable_v<Vec2>);
static_assert(std::is_standard_layout_v<Vec3> && std::is_trivially_copyable_v<Vec3>);
static_assert(std::is_standard_layout_v<Vec4> && std::is_trivially_copyable_v<Vec4>);
static_assert(std::is_standard_layout_v<Quat> && std::is_trivially_copyable_v<Quat>);
static_assert(std::is_standard_layout_v<Mat4> && std::is_trivially_copyable_v<Mat4>);
static_assert(!std::is_constructible_v<Mat4, float>);
namespace {
int failures = 0, checks = 0;
void check(bool b, const char* s, int line) {
    ++checks;
    if (!b) {
        ++failures;
        std::cerr << "FAIL " << line << ": " << s << '\n';
    }
}
#define CHECK(x) check((x), #x, __LINE__)
constexpr float e = 2.e-5f;
const float nan = std::numeric_limits<float>::quiet_NaN();
const float inf = std::numeric_limits<float>::infinity();
bool near(float a, float b, float tol = e) { return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tol; }
template <class T, class Function> void preserves_failure(T& out, Function function) {
    std::array<unsigned char, sizeof(T)> saved{};
    std::memcpy(saved.data(), &out, sizeof(T));
    CHECK(!function());
    CHECK(std::memcmp(saved.data(), &out, sizeof(T)) == 0);
}
template <class V> void vector_boundary_contract() {
    using S = typename V::value_type;
    V bad{}, out(S(9));
    preserves_failure(out, [&] { return try_normalize(bad, out); });
    preserves_failure(bad, [&] { return try_normalize(bad, bad); });
    bad[0] = std::numeric_limits<S>::quiet_NaN();
    preserves_failure(out, [&] { return try_normalize(bad, out); });
    preserves_failure(bad, [&] { return try_normalize(bad, bad); });
    bad[0] = std::numeric_limits<S>::infinity();
    preserves_failure(out, [&] { return try_normalize(bad, out); });
    CHECK(normalize(bad) == V{});
    for (S value :
         {std::numeric_limits<S>::min(), std::numeric_limits<S>::denorm_min(), std::numeric_limits<S>::max()}) {
        V v;
        v[0] = value;
        CHECK(try_normalize(v, v));
        CHECK(v[0] == S(1));
        for (std::size_t i = 1; i < V::component_count; ++i) CHECK(v[i] == S(0));
    }
    CHECK(!nearly_equal(V(S(1)), V(S(1)), std::numeric_limits<S>::quiet_NaN()));
    CHECK(!nearly_equal(V(S(1)), V(S(1)), S(-1)));
    for (S invalid : {std::numeric_limits<S>::quiet_NaN(), std::numeric_limits<S>::infinity(),
                      -std::numeric_limits<S>::infinity()}) {
        for (std::size_t lane = 0; lane < V::component_count; ++lane) {
            V invalid_vector(S(2));
            invalid_vector[lane] = invalid;
            for (V result : {min(V(S(1)), invalid_vector), min(invalid_vector, V(S(1))), max(V(S(1)), invalid_vector),
                             max(invalid_vector, V(S(1))), V(S(1)) / invalid_vector, invalid_vector / V(S(1))}) {
                CHECK(std::isnan(result[lane]));
                CHECK(!is_finite(result));
            }
        }
        const V divided = V(S(1)) / invalid;
        for (std::size_t lane = 0; lane < V::component_count; ++lane) CHECK(std::isnan(divided[lane]));
    }
}
void defaults_and_vectors() {
    CHECK(Vec2{} == Vec2(0));
    CHECK(Vec3{} == Vec3(0));
    CHECK(Vec4{} == Vec4(0));
    CHECK(Quat{} == Quat(0, 0, 0, 1));
    const Mat4 zero{};
    for (std::size_t c = 0; c < 4; ++c) CHECK(zero[c] == Vec4(0));
    const auto id = identity();
    for (std::size_t r = 0; r < 4; ++r)
        for (std::size_t c = 0; c < 4; ++c) CHECK(id.at(r, c) == (r == c ? 1.f : 0.f));
    CHECK(Vec3(Vec4(1, 2, 3, 99)) == Vec3(1, 2, 3));
    CHECK(Vec4(Vec3(1, 2, 3), 4) == Vec4(1, 2, 3, 4));
    CHECK(DVec2(Vec2(2, 3)) == DVec2(2, 3));
    CHECK(Vec3(DVec3(2, 3, 4)) == Vec3(2, 3, 4));
    Vec3 dirty(3, 4, 0);
    std::memcpy(reinterpret_cast<unsigned char*>(&dirty) + 12, &nan, 4);
    CHECK(dirty == Vec3(3, 4, 0));
    CHECK(is_finite(dirty));
    CHECK(near(length(dirty), 5));
    CHECK(normalize(dirty) == Vec3(.6f, .8f, 0));
    CHECK(dirty + Vec3(1) == Vec3(4, 5, 1));
    CHECK(cross(Vec3(1, 0, 0), Vec3(0, 1, 0)) == Vec3(0, 0, 1));
    CHECK(dot(Vec4(1, 2, 3, 4), Vec4(2, -1, 2, 1)) == 10);
    CHECK(Vec2(4, 8) / Vec2(2, 4) == Vec2(2));
    CHECK(Vec2(2, 3) * Vec2(4, 5) == Vec2(8, 15));
    CHECK(-Vec3(1, -2, 3) == Vec3(-1, 2, -3));
    CHECK(2.f * Vec3(1, 2, 3) == Vec3(2, 4, 6));
    Vec3 v(1, 2, 3);
    v += Vec3(2);
    v -= Vec3(1);
    v *= 2;
    v /= 2;
    CHECK(v == Vec3(2, 3, 4));
    CHECK(min(Vec3(1, 4, -1), Vec3(2, 3, 0)) == Vec3(1, 3, -1));
    CHECK(max(Vec3(1, 4, -1), Vec3(2, 3, 0)) == Vec3(2, 4, 0));
    CHECK(length_squared(Vec2(3, 4)) == 25);
    CHECK(near(to_radians(180), kPi));
    CHECK(near(to_degrees(kPi / 2), 90));
    for (float s : {1.e-38f, 1.e-20f, 1.f, 1.e20f, 1.e38f}) {
        Vec3 n;
        CHECK(try_normalize(Vec3(0, s, 0), n));
        CHECK(n == Vec3(0, 1, 0));
    }
    Vec3 alias(3, 4, 0);
    CHECK(try_normalize(alias, alias));
    CHECK(nearly_equal(alias, Vec3(.6f, .8f, 0), e));
    for (Vec3 bad : {Vec3{}, Vec3(nan, 1, 2), Vec3(inf, 1, 2)}) {
        Vec3 out(9, 8, 7);
        CHECK(!try_normalize(bad, out));
        CHECK(out == Vec3(9, 8, 7));
        CHECK(normalize(bad) == Vec3{});
    }
    Vec3 zeros;
    CHECK(!try_normalize(zeros, zeros));
    CHECK(zeros == Vec3{});
    CHECK(!is_finite(Vec2(inf, 1)));
    CHECK(!is_finite(Vec4(1, 2, 3, nan)));
    CHECK(!nearly_equal(Vec3(1), Vec3(1), -1));
    CHECK(!nearly_equal(Vec3(1), Vec3(1), inf));
    CHECK(!nearly_equal(Vec3(nan, 0, 0), Vec3(nan, 0, 0), 1));
    CHECK(nearly_equal(Vec3(1), Vec3(1.0001f), .001f));
    CHECK(Vec3(1) != Vec3(1.0001f));
    CHECK(cross(DVec3(1, 0, 0), DVec3(0, 1, 0)) == DVec3(0, 0, 1));
    DVec3 dv(1.e-200, 0, 0);
    CHECK(try_normalize(dv, dv));
    CHECK(dv == DVec3(1, 0, 0));
    CHECK(try_normalize(DVec3(1.e300, 0, 0), dv));
    CHECK(dv == DVec3(1, 0, 0));
    const DVec3 precise(1.00000000001, 2, 3);
    CHECK((precise - DVec3(1, 2, 3)).x > 0);
    CHECK(nearly_equal(normalize(DVec2(3, 4)), DVec2(.6, .8), 1.e-15));
    CHECK(dot(dirty, Vec3(1)) == 7);
    CHECK(nearly_equal(dirty, Vec3(3, 4, 0), 0));
    CHECK(cross(dirty, Vec3(0, 0, 1)) == Vec3(4, -3, 0));
    CHECK(Vec4(dirty, 1) == Vec4(3, 4, 0, 1));
    vector_boundary_contract<Vec2>();
    vector_boundary_contract<Vec3>();
    vector_boundary_contract<Vec4>();
    vector_boundary_contract<DVec2>();
    vector_boundary_contract<DVec3>();
}
void quaternions() {
    const Quat z = axis_angle(Vec3(0, 0, 2), kPi / 2);
    CHECK(is_unit(z));
    CHECK(nearly_equal(rotate(z, Vec3(1, 0, 0)), Vec3(0, 1, 0), e));
    CHECK(nearly_equal(z * inverse(z), Quat{}, e));
    CHECK(conjugate(Quat(1, 2, 3, 4)) == Quat(-1, -2, -3, 4));
    const auto x = axis_angle(Vec3(1, 0, 0), kPi / 2), y = axis_angle(Vec3(0, 1, 0), kPi / 2);
    CHECK(nearly_equal(rotate(y * x, Vec3(0, 1, 0)), Vec3(1, 0, 0), e));
    for (float s : {1.e-38f, 1.e38f}) {
        Quat n;
        CHECK(try_normalize(Quat(s, 0, 0, 0), n));
        CHECK(n == Quat(1, 0, 0, 0));
    }
    Quat q(1, 2, 3, 4);
    CHECK(try_normalize(q, q));
    CHECK(is_unit(q));
    CHECK(try_inverse(q, q));
    CHECK(is_unit(q));
    Quat inv;
    CHECK(try_inverse(Quat(0, 0, 0, 2), inv));
    CHECK(inv == Quat(0, 0, 0, .5));
    for (Quat bad : {Quat(0, 0, 0, 0), Quat(nan, 0, 0, 1), Quat(0, inf, 0, 1)}) {
        Quat out(1, 2, 3, 4);
        CHECK(!try_normalize(bad, out));
        CHECK(out == Quat(1, 2, 3, 4));
        CHECK(!try_inverse(bad, out));
        CHECK(out == Quat(1, 2, 3, 4));
        CHECK(normalize(bad) == Quat{});
        CHECK(inverse(bad) == Quat{});
    }
    Quat out(1, 2, 3, 4);
    CHECK(!try_axis_angle(Vec3{}, 1, out));
    CHECK(out == Quat(1, 2, 3, 4));
    CHECK(!try_axis_angle(Vec3(1, 0, 0), nan, out));
    CHECK(out == Quat(1, 2, 3, 4));
    CHECK(axis_angle(Vec3{}, 1) == Quat{});
    for (Quat bad : {Quat(0, 0, 0, 2), Quat(0, 0, 0, 0), Quat(nan, 0, 0, 1)}) {
        Vec3 v(1, 2, 3);
        CHECK(!try_rotate(bad, v, v));
        CHECK(v == Vec3(1, 2, 3));
        CHECK(rotate(bad, v) == Vec3{});
        Mat4 m = identity();
        CHECK(!try_from_quat(bad, m));
        CHECK(m == identity());
        CHECK(from_quat(bad) == Mat4{});
    }
    Vec3 v(inf, 0, 0), saved(4, 5, 6);
    CHECK(!try_rotate(Quat{}, v, saved));
    CHECK(saved == Vec3(4, 5, 6));
    for (Quat bad : {Quat(0, 0, 0, 0), Quat(nan, 0, 0, 1), Quat(inf, 0, 0, 1)}) {
        preserves_failure(bad, [&] { return try_normalize(bad, bad); });
        preserves_failure(bad, [&] { return try_inverse(bad, bad); });
    }
    Quat inverse_out(1, 2, 3, 4);
    preserves_failure(
        inverse_out, [&] { return try_inverse(Quat(std::numeric_limits<float>::denorm_min(), 0, 0, 0), inverse_out); });
    CHECK(inverse(Quat(std::numeric_limits<float>::denorm_min(), 0, 0, 0)) == Quat{});
    for (float s : {1.e-38f, 1.e38f}) {
        Quat result;
        CHECK(try_inverse(Quat(s, 0, 0, 0), result));
        CHECK(near(result.x * s, -1));
        CHECK(result.y == 0 && result.z == 0 && result.w == 0);
    }
}
void matrices() {
    Mat4 a{Vec4(1, 2, 3, 4), Vec4(5, 6, 7, 8), Vec4(9, 10, 11, 12), Vec4(13, 14, 15, 16)};
    CHECK(a.at(2, 1) == 7);
    CHECK(transpose(a).at(1, 2) == 7);
    CHECK(a * Vec4(1, 2, 3, 4) == Vec4(90, 100, 110, 120));
    Mat4 b{Vec4(2, 0, 0, 0), Vec4(0, 3, 0, 0), Vec4(0, 0, 4, 0), Vec4(1, 2, 3, 1)};
    const Mat4 expected{Vec4(2, 4, 6, 8), Vec4(15, 18, 21, 24), Vec4(36, 40, 44, 48), Vec4(51, 58, 65, 72)};
    CHECK(a * b == expected);
    CHECK(a * identity() == a);
    CHECK(identity() * a == a);
    const auto m = trs(Vec3(3, 4, 5), axis_angle(Vec3(0, 0, 1), kPi / 2), Vec3(2, 3, 4));
    CHECK(nearly_equal(transform_point(m, Vec3(1, 0, 0)), Vec3(3, 6, 5), e));
    CHECK(nearly_equal(transform_vector(m, Vec3(1, 0, 0)), Vec3(0, 2, 0), e));
    CHECK(nearly_equal(transform_point(scale(Vec3(2)) * translation(Vec3(1, 0, 0)), Vec3(1, 0, 0)), Vec3(4, 0, 0), e));
    const auto view = look_at(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1, 0));
    CHECK(nearly_equal(transform_point(view, Vec3(1, 2, 0)), Vec3(1, 2, -5), e));
    CHECK(nearly_equal(rotation(Vec3(0, 1, 0), kPi / 2) * Vec4(0, 0, -1, 1), Vec4(-1, 0, 0, 1), e));
    Mat4 iv;
    CHECK(try_inverse(m, iv));
    CHECK(nearly_equal(iv * m, identity(), e));
    Mat4 alias = m;
    CHECK(try_inverse(alias, alias));
    CHECK(nearly_equal(alias * m, identity(), e));
    Mat4 tiny;
    for (std::size_t i = 0; i < 4; ++i) tiny.at(i, i) = 1.e-20f;
    CHECK(try_inverse(tiny, iv));
    CHECK(nearly_equal(tiny * iv, identity(), e));
    const Mat4 huge = translation(Vec3(1.e30f, -2.e30f, 3.e30f));
    CHECK(try_inverse(huge, iv));
    CHECK(huge * iv == identity());
    Mat4 anisotropic = identity();
    anisotropic.at(0, 0) = 1.e-20f;
    Mat4 preserved = translation(Vec3(7, 8, 9));
    iv = preserved;
    CHECK(!try_inverse(anisotropic, iv));
    CHECK(iv == preserved);
    Mat4 near_singular = identity();
    near_singular.at(0, 1) = 1;
    near_singular.at(1, 0) = 1;
    near_singular.at(1, 1) = 1.0000001f;
    CHECK(!try_inverse(near_singular, iv));
    CHECK(try_inverse(near_singular, iv, 0));
    for (Mat4 bad : {Mat4{}, a, near_singular}) {
        Mat4 out = translation(Vec3(7, 8, 9)), saved = out;
        CHECK(!try_inverse(bad, out));
        CHECK(out == saved);
        CHECK(inverse(bad) == Mat4{});
    }
    Mat4 n = identity();
    n.at(1, 2) = nan;
    iv = identity();
    CHECK(!try_inverse(n, iv));
    CHECK(iv == identity());
    CHECK(!try_inverse(identity(), iv, -1));
    CHECK(!try_inverse(identity(), iv, inf));
    CHECK(!nearly_equal(identity(), identity(), nan));
    CHECK(!nearly_equal(n, n, 1));
    Mat4 out = identity();
    CHECK(!try_translation(Vec3(inf, 0, 0), out));
    CHECK(out == identity());
    CHECK(translation(Vec3(nan, 0, 0)) == Mat4{});
    CHECK(!try_scale(Vec3(1, 0, 1), out));
    CHECK(out == identity());
    CHECK(scale(Vec3(1, 0, 1)) == Mat4{});
    CHECK(!try_rotation(Vec3{}, 1, out));
    CHECK(out == identity());
    CHECK(rotation(Vec3{}, 1) == Mat4{});
    CHECK(!try_trs(Vec3{}, Quat(0, 0, 0, 2), Vec3(1), out));
    CHECK(out == identity());
    CHECK(trs(Vec3{}, Quat(0, 0, 0, 2), Vec3(1)) == Mat4{});
    CHECK(!try_look_at(Vec3{}, Vec3{}, Vec3(0, 1, 0), out));
    CHECK(out == identity());
    CHECK(!try_look_at(Vec3{}, Vec3(0, 0, -1), Vec3(0, 0, 1), out));
    CHECK(out == identity());
    CHECK(look_at(Vec3{}, Vec3{}, Vec3(0, 1, 0)) == Mat4{});
    for (Mat4 bad : {Mat4{}, n}) {
        Vec3 v(7, 8, 9);
        CHECK(!try_transform_point(bad, v, v));
        CHECK(v == Vec3(7, 8, 9));
        CHECK(!try_transform_vector(bad, v, v));
        CHECK(v == Vec3(7, 8, 9));
        CHECK(transform_point(bad, v) == Vec3{});
        CHECK(transform_vector(bad, v) == Vec3{});
    }
    Vec3 v(7, 8, 9);
    CHECK(try_transform_point(identity(), v, v));
    CHECK(v == Vec3(7, 8, 9));
    CHECK(try_transform_vector(identity(), v, v));
    CHECK(v == Vec3(7, 8, 9));
    const Mat4 triangular{Vec4(2, 0, 0, 0), Vec4(1, 4, 0, 0), Vec4(0, 1, 5, 0), Vec4(3, 2, 1, 2)};
    const Mat4 triangular_inverse{Vec4(.5f, 0, 0, 0), Vec4(-.125f, .25f, 0, 0), Vec4(.025f, -.05f, .2f, 0),
                                  Vec4(-.6375f, -.225f, -.1f, .5f)};
    CHECK(nearly_equal(inverse(triangular), triangular_inverse, e));
    const Mat4 dependent{Vec4(1, 2, 0, 3), Vec4(2, 4, 1, 1), Vec4(3, 6, 2, 4), Vec4(4, 8, 1, 2)};
    Mat4 almost_dependent = dependent;
    almost_dependent.at(1, 1) = std::nextafter(4.f, inf);
    preserves_failure(out, [&] { return try_inverse(almost_dependent, out); });
    CHECK(try_inverse(almost_dependent, iv, 0));
    for (float magnitude : {1.e-20f, 1.f, 1.e20f}) {
        Mat4 singular = dependent;
        for (std::size_t c = 0; c < 4; ++c) singular[c] *= magnitude;
        preserves_failure(out, [&] { return try_inverse(singular, out); });
        preserves_failure(out, [&] { return try_inverse(singular, out, 0); });
        Vec3 v(1, 2, 3);
        preserves_failure(v, [&] { return try_transform_point(singular, v, v); });
        preserves_failure(v, [&] { return try_transform_vector(singular, v, v); });
        Frustum frustum = extract_frustum(perspective(1, 1, 1, 10));
        preserves_failure(frustum, [&] { return try_extract_frustum(singular, frustum); });
    }
    for (Mat4 bad : {Mat4{}, n, near_singular, anisotropic})
        preserves_failure(bad, [&] { return try_inverse(bad, bad); });
    for (float bad : {nan, inf}) {
        preserves_failure(out, [&] { return try_translation(Vec3(bad, 0, 0), out); });
        preserves_failure(out, [&] { return try_scale(Vec3(1, bad, 1), out); });
        preserves_failure(out, [&] { return try_rotation(Vec3(1, 0, 0), bad, out); });
        preserves_failure(out, [&] { return try_rotation(Vec3(1, bad, 0), 1, out); });
        preserves_failure(out, [&] { return try_from_quat(Quat(0, 0, bad, 1), out); });
        preserves_failure(out, [&] { return try_trs(Vec3(0, bad, 0), Quat{}, Vec3(1), out); });
        preserves_failure(out, [&] { return try_trs(Vec3{}, Quat{}, Vec3(bad, 1, 1), out); });
        preserves_failure(out, [&] { return try_look_at(Vec3(bad, 0, 0), Vec3{}, Vec3(0, 1, 0), out); });
        preserves_failure(out, [&] { return try_look_at(Vec3{}, Vec3(0, bad, 0), Vec3(0, 1, 0), out); });
        preserves_failure(out, [&] { return try_look_at(Vec3{}, Vec3(0, 0, -1), Vec3(0, 1, bad), out); });
        Vec3 bvec(bad, 1, 2);
        preserves_failure(bvec, [&] { return try_transform_point(identity(), bvec, bvec); });
        preserves_failure(bvec, [&] { return try_transform_vector(identity(), bvec, bvec); });
    }
    const Mat4 over = scale(Vec3(std::numeric_limits<float>::max()));
    Vec3 extreme(2, 2, 2);
    preserves_failure(extreme, [&] { return try_transform_point(over, extreme, extreme); });
    preserves_failure(extreme, [&] { return try_transform_vector(over, extreme, extreme); });
}
void multiplication_corpus() {
    // Exact small-integer oracle is independent of either float backend, and
    // catches wrong row/column order, signs, and lanes with no tolerance escape.
    std::uint32_t state = 0x4216u;
    const auto next = [&]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<int>((state >> 16) % 17) - 8;
    };
    for (int sample = 0; sample < 256; ++sample) {
        Mat4 a, b;
        for (std::size_t c = 0; c < 4; ++c)
            for (std::size_t r = 0; r < 4; ++r) {
                a.at(r, c) = static_cast<float>(next());
                b.at(r, c) = static_cast<float>(next());
            }
        const Mat4 product = a * b;
        for (std::size_t r = 0; r < 4; ++r)
            for (std::size_t c = 0; c < 4; ++c) {
                int expected = 0;
                for (std::size_t k = 0; k < 4; ++k)
                    expected += static_cast<int>(a.at(r, k)) * static_cast<int>(b.at(k, c));
                CHECK(product.at(r, c) == static_cast<float>(expected));
            }
        const Vec4 v(static_cast<float>(next()), static_cast<float>(next()), static_cast<float>(next()),
                     static_cast<float>(next()));
        const Vec4 result = a * v;
        for (std::size_t r = 0; r < 4; ++r) {
            int expected = 0;
            for (std::size_t k = 0; k < 4; ++k) expected += static_cast<int>(a.at(r, k)) * static_cast<int>(v[k]);
            CHECK(result[r] == static_cast<float>(expected));
        }
    }
}
void singular_dependency_corpus() {
    const auto rejects = [](const Mat4& m) {
        Mat4 inverse_out = identity();
        preserves_failure(inverse_out, [&] { return try_inverse(m, inverse_out, 0); });
        preserves_failure(inverse_out, [&] { return try_inverse(m, inverse_out); });
        Vec3 point(9, 8, 7), vector(9, 8, 7);
        preserves_failure(point, [&] { return try_transform_point(m, Vec3(1, 2, 3), point); });
        preserves_failure(vector, [&] { return try_transform_vector(m, Vec3(1, 2, 3), vector); });
        Frustum frustum = extract_frustum(perspective(1, 1, 1, 10));
        preserves_failure(frustum, [&] { return try_extract_frustum(m, frustum); });
    };
    // Hand supplied counterexample: fourth row is exactly first + second.
    const Mat4 reported{Vec4(1, -7, 5, -6), Vec4(3, 3, -2, 6), Vec4(3, 5, 2, 8), Vec4(5, -6, -2, -1)};
    rejects(reported);
    std::uint32_t state = 0x17beef42u;
    const auto next = [&]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<int>((state >> 16) % 17) - 8;
    };
    for (int sample = 0; sample < 256; ++sample) {
        Mat4 projective;
        for (std::size_t r = 0; r < 3; ++r)
            for (std::size_t c = 0; c < 4; ++c) projective.at(r, c) = static_cast<float>(next());
        for (std::size_t c = 0; c < 4; ++c) projective.at(3, c) = projective.at(0, c) + projective.at(1, c);
        Mat4 affine = projective;
        for (std::size_t c = 0; c < 3; ++c) affine.at(2, c) = affine.at(0, c) + affine.at(1, c);
        affine[3] = Vec4(1.e30f, -2.e30f, 3.e30f, 1);
        affine.at(3, 0) = 0;
        affine.at(3, 1) = 0;
        affine.at(3, 2) = 0;
        rejects(affine);
        for (float magnitude : {0x1p-80f, 1.f, 0x1p80f}) {
            Mat4 scaled = projective;
            for (auto& column : scaled.columns) column *= magnitude;
            rejects(scaled);
        }
    }
}
void projections_and_frusta() {
    Mat4 inverse_unrepresentable;
    for (std::size_t i = 0; i < 4; ++i) inverse_unrepresentable.at(i, i) = 1.e-39f;
    Vec3 point;
    CHECK(try_transform_point(inverse_unrepresentable, Vec3(1, 2, 3), point));
    CHECK(nearly_equal(point, Vec3(1, 2, 3), e));
    Frustum scaled_frustum;
    CHECK(try_extract_frustum(inverse_unrepresentable, scaled_frustum));
    CHECK(scaled_frustum.contains_point(Vec3(0, 0, .5f)));
    const auto tiny_near = perspective(1, 1, 1.e-8f, 1);
    CHECK(tiny_near != Mat4{});
    CHECK(near(transform_point(tiny_near, Vec3(0, 0, -1.e-8f)).z, 0));
    CHECK(near(transform_point(tiny_near, Vec3(0, 0, -1)).z, 1));
    const auto long_ortho = orthographic(-1, 1, -1, 1, 1, 1.e10f);
    CHECK(long_ortho != Mat4{});
    CHECK(near(transform_point(long_ortho, Vec3(0, 0, -1.e10f)).z, 1));
    CHECK(is_valid(extract_frustum(long_ortho)));
    Mat4 checked_projection;
    CHECK(try_perspective(1, 1, 1.e-8f, 1, checked_projection));
    Vec3 checked_point;
    CHECK(try_transform_point(checked_projection, Vec3(0, 0, -1.e-8f), checked_point));
    CHECK(near(checked_point.z, 0));
    Mat4 rounded_far = perspective(1, 1, 1, 1.e9f);
    Frustum far_out = scaled_frustum;
    preserves_failure(far_out, [&] { return try_extract_frustum(rounded_far, far_out); });
    for (bool yd : {false, true})
        for (bool rz : {false, true}) {
            const ClipSpaceConvention c{yd, rz};
            const auto p = perspective(kPi / 2, 2, 1, 11, c);
            CHECK(near(p.at(0, 0), .5));
            CHECK(near(p.at(1, 1), yd ? -1.f : 1.f));
            CHECK(near(p.at(2, 2), rz ? .1f : -1.1f));
            CHECK(near(p.at(2, 3), rz ? 1.1f : -1.1f));
            CHECK(p.at(3, 2) == -1);
            CHECK(near(transform_point(p, Vec3(0, 0, -1)).z, rz ? 1.f : 0.f));
            CHECK(near(transform_point(p, Vec3(0, 0, -11)).z, rz ? 0.f : 1.f));
            CHECK(nearly_equal(transform_point(p, Vec3(4, 2, -2)), Vec3(1, yd ? -1.f : 1.f, rz ? .45f : .55f), e));
            CHECK(nearly_equal(transform_point(p * look_at(Vec3(0, 0, 5), Vec3{}, Vec3(0, 1, 0)), Vec3(0, 0, 4)),
                               Vec3(0, 0, rz ? 1.f : 0.f), e));
            const auto o = orthographic(-2, 6, -3, 1, 2, 10, c);
            CHECK(near(o.at(0, 0), .25));
            CHECK(near(o.at(0, 3), -.5));
            CHECK(near(o.at(1, 1), yd ? -.5f : .5f));
            CHECK(near(o.at(1, 3), yd ? -.5f : .5f));
            CHECK(near(o.at(2, 2), rz ? .125f : -.125f));
            CHECK(near(o.at(2, 3), rz ? 1.25f : -.25f));
            CHECK(nearly_equal(transform_point(o, Vec3(-2, -3, -2)), Vec3(-1, yd ? 1.f : -1.f, rz ? 1.f : 0.f), e));
            CHECK(nearly_equal(transform_point(o, Vec3(6, 1, -10)), Vec3(1, yd ? -1.f : 1.f, rz ? 0.f : 1.f), e));
            Frustum f;
            CHECK(try_extract_frustum(o, f, c));
            CHECK(is_valid(f));
            const Plane wants[6] = {{Vec3(1, 0, 0), 2},  {Vec3(-1, 0, 0), 6},  {Vec3(0, 1, 0), 3},
                                    {Vec3(0, -1, 0), 1}, {Vec3(0, 0, -1), -2}, {Vec3(0, 0, 1), 10}};
            for (std::size_t i = 0; i < 6; ++i) {
                CHECK(nearly_equal(f.planes[i].normal, wants[i].normal, e));
                CHECK(near(f.planes[i].offset, wants[i].offset));
            }
            CHECK(contains_point(f, Vec3(0, 0, -5)));
            CHECK(!contains_point(f, Vec3(7, 0, -5)));
            CHECK(f.contains_point(Vec3(0, 0, -5)));
            CHECK(f.intersects_sphere(Vec3(0, 0, -5), 1));
            CHECK(f.intersects_aabb(Vec3(-1, -1, -6), Vec3(1, 0, -4)));
            CHECK(!contains_point(f, Vec3(0, 2, -5)));
            CHECK(!contains_point(f, Vec3(0, 0, -1)));
            CHECK(!contains_point(f, Vec3(0, 0, -11)));
            CHECK(intersects_sphere(f, Vec3(6.5f, 0, -5), .5f));
            CHECK(!intersects_sphere(f, Vec3(6.6f, 0, -5), .5f));
            CHECK(intersects_aabb(f, Vec3(-1, -1, -6), Vec3(1, 0, -4)));
            CHECK(!intersects_aabb(f, Vec3(7, -1, -6), Vec3(8, 0, -4)));
            CHECK(!intersects_sphere(f, Vec3{}, -1));
            CHECK(!intersects_aabb(f, Vec3(2), Vec3(1)));
            CHECK(!contains_point(f, Vec3(nan, 0, 0)));
            Frustum pf;
            CHECK(try_extract_frustum(p, pf, c));
            CHECK(contains_point(pf, Vec3(0, 0, -2)));
            CHECK(!contains_point(pf, Vec3(10, 0, -2)));
            CHECK(near(length(pf.planes[kNear].normal), 1));
            CHECK(pf.planes[kNear].normal.z < 0);
            CHECK(pf.planes[kFar].normal.z > 0);
            Vec3 v(1, 2, 0);
            CHECK(!try_transform_point(p, v, v));
            CHECK(v == Vec3(1, 2, 0));
        }
    const auto def = perspective(kPi / 2, 1, 1, 10);
    CHECK(def == perspective(kPi / 2, 1, 1, 10, {false, false}));
    for (float bad : {0.f, -1.f, kPi, inf, nan}) {
        Mat4 out = identity();
        CHECK(!try_perspective(bad, 1, 1, 10, out));
        CHECK(out == identity());
        CHECK(perspective(bad, 1, 1, 10) == Mat4{});
    }
    for (float bad : {0.f, -1.f, inf, nan}) {
        Mat4 out = identity();
        CHECK(!try_perspective(1, bad, 1, 10, out));
        CHECK(out == identity());
    }
    Mat4 out = identity();
    CHECK(!try_perspective(1, 1, 10, 1, out));
    CHECK(out == identity());
    CHECK(!try_perspective(1, 1, 0, 1, out));
    CHECK(out == identity());
    CHECK(!try_orthographic(0, 0, -1, 1, 1, 10, out));
    CHECK(out == identity());
    CHECK(!try_orthographic(-1, 1, 1, -1, 1, 10, out));
    CHECK(out == identity());
    CHECK(!try_orthographic(-1, 1, -1, 1, -1, 10, out));
    CHECK(out == identity());
    CHECK(orthographic(0, 0, -1, 1, 1, 10) == Mat4{});
    Plane p{Vec3(0, 2, 0), 4};
    CHECK(try_normalize(p, p));
    CHECK(p.normal == Vec3(0, 1, 0) && p.offset == 2);
    CHECK(signed_distance(p, Vec3(0, 3, 0)) == 5);
    for (Plane bad : {Plane{}, Plane{Vec3(1, 0, 0), inf}, Plane{Vec3(nan, 0, 0), 0}}) {
        Plane saved{Vec3(1, 0, 0), 9}, v = saved;
        CHECK(!try_normalize(bad, v));
        CHECK(v.normal == saved.normal && v.offset == saved.offset);
    }
    CHECK(normalize(Plane{}).normal == Vec3{});
    Frustum f = extract_frustum(def), saved = f;
    CHECK(!try_extract_frustum(Mat4{}, f));
    for (std::size_t i = 0; i < 6; ++i)
        CHECK(f.planes[i].normal == saved.planes[i].normal && f.planes[i].offset == saved.planes[i].offset);
    Mat4 bad = identity();
    bad.at(1, 2) = nan;
    CHECK(!try_extract_frustum(bad, f));
    CHECK(!is_valid(Frustum{}));
    CHECK(!contains_point(Frustum{}, Vec3{}));
    CHECK(!intersects_sphere(Frustum{}, Vec3{}, 1));
    CHECK(!intersects_aabb(Frustum{}, Vec3{}, Vec3(1)));
    for (float bad_value : {nan, inf}) {
        for (int argument = 0; argument < 4; ++argument) {
            float args[4] = {1, 1, 1, 10};
            args[argument] = bad_value;
            preserves_failure(out, [&] { return try_perspective(args[0], args[1], args[2], args[3], out); });
        }
        for (int argument = 0; argument < 6; ++argument) {
            float args[6] = {-1, 1, -1, 1, 1, 10};
            args[argument] = bad_value;
            preserves_failure(
                out, [&] { return try_orthographic(args[0], args[1], args[2], args[3], args[4], args[5], out); });
        }
        Plane plane{Vec3(bad_value, 0, 0), 1};
        preserves_failure(plane, [&] { return try_normalize(plane, plane); });
    }
    Plane zero_plane;
    preserves_failure(zero_plane, [&] { return try_normalize(zero_plane, zero_plane); });
    for (Mat4 bad_matrix : {Mat4{}, bad}) preserves_failure(f, [&] { return try_extract_frustum(bad_matrix, f); });
    const Plane tiny_plane{Vec3(0, 1.e-38f, 0), 1.e-38f};
    CHECK(normalize(tiny_plane).normal == Vec3(0, 1, 0));
    CHECK(normalize(tiny_plane).offset == 1);
    const Plane huge_plane{Vec3(0, 1.e38f, 0), 1.e38f};
    CHECK(normalize(huge_plane).normal == Vec3(0, 1, 0));
    CHECK(normalize(huge_plane).offset == 1);
    Frustum invalid = f;
    invalid.planes[kLeft].normal *= 2;
    CHECK(!is_valid(invalid));
    CHECK(!invalid.contains_point(Vec3(0, 0, -5)));
    CHECK(!invalid.intersects_sphere(Vec3(0, 0, -5), 1));
    CHECK(!invalid.intersects_aabb(Vec3(-1), Vec3(1)));
}
void floating_environment() {
    const int previous = std::fegetround();
    CHECK(std::fesetround(FE_DOWNWARD) == 0);
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    const unsigned control = _mm_getcsr() & ~0x3fu;
#endif
    const Mat4 m = translation(Vec3(1, 2, 3)) * scale(Vec3(2, 3, 4));
    CHECK(m * Vec4(1, 1, 1, 1) == Vec4(3, 5, 7, 1));
    CHECK(is_finite(normalize(Vec3(3, 4, 0))));
    CHECK(is_finite(inverse(m)));
    CHECK(is_finite(perspective(1, 1, 1, 10)));
    CHECK(is_valid(extract_frustum(orthographic(-1, 1, -1, 1, 1, 10))));
    CHECK(std::fegetround() == FE_DOWNWARD);
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    CHECK((_mm_getcsr() & ~0x3fu) == control);
#endif
    CHECK(std::fesetround(previous) == 0);
}
}
int main() {
    const int rounding = std::fegetround();
    defaults_and_vectors();
    quaternions();
    matrices();
    multiplication_corpus();
    singular_dependency_corpus();
    projections_and_frusta();
    floating_environment();
    CHECK(std::fegetround() == rounding);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
