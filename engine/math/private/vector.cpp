#include "math.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace aeg::math {
namespace {
template <class S> S component_min(S a, S b) noexcept {
    return std::isfinite(a) && std::isfinite(b) ? std::min(a, b) : std::numeric_limits<S>::quiet_NaN();
}
template <class S> S component_max(S a, S b) noexcept {
    return std::isfinite(a) && std::isfinite(b) ? std::max(a, b) : std::numeric_limits<S>::quiet_NaN();
}
template <class S> S component_divide(S a, S b) noexcept {
    return std::isfinite(a) && std::isfinite(b) ? a / b : std::numeric_limits<S>::quiet_NaN();
}
template <class V> bool finite_vector(V v) noexcept {
    for (std::size_t i = 0; i < V::component_count; ++i)
        if (!std::isfinite(v[i])) return false;
    return true;
}
template <class V> bool normalize_vector(V v, V& out) noexcept {
    if (!finite_vector(v)) return false;
    long double magnitude = 0;
    for (std::size_t i = 0; i < V::component_count; ++i)
        magnitude = std::max(magnitude, std::abs(static_cast<long double>(v[i])));
    if (magnitude == 0) return false;
    long double sum = 0;
    for (std::size_t i = 0; i < V::component_count; ++i) {
        const long double s = v[i] / magnitude;
        sum += s * s;
    }
    const long double norm = std::sqrt(sum);
    V result;
    for (std::size_t i = 0; i < V::component_count; ++i)
        result[i] = static_cast<typename V::value_type>((v[i] / magnitude) / norm);
    if (!finite_vector(result)) return false;
    out = result;
    return true;
}
template <class V> typename V::value_type vector_length(V v) noexcept {
    long double magnitude = 0;
    for (std::size_t i = 0; i < V::component_count; ++i) {
        if (!std::isfinite(v[i])) return static_cast<typename V::value_type>(std::abs(v[i]));
        magnitude = std::max(magnitude, std::abs(static_cast<long double>(v[i])));
    }
    if (magnitude == 0) return 0;
    long double sum = 0;
    for (std::size_t i = 0; i < V::component_count; ++i) {
        const long double s = v[i] / magnitude;
        sum += s * s;
    }
    return static_cast<typename V::value_type>(magnitude * std::sqrt(sum));
}
template <class V> bool near_vector(V a, V b, typename V::value_type tolerance) noexcept {
    if (!std::isfinite(tolerance) || tolerance < 0 || !finite_vector(a) || !finite_vector(b)) return false;
    for (std::size_t i = 0; i < V::component_count; ++i)
        if (std::abs(static_cast<long double>(a[i]) - b[i]) > tolerance) return false;
    return true;
}
}
#define AEG_VECTOR_IMPL(V, S)                                                                                          \
    V operator+(V a, V b) noexcept {                                                                                   \
        V r;                                                                                                           \
        for (std::size_t i = 0; i < V::component_count; ++i) r[i] = a[i] + b[i];                                       \
        return r;                                                                                                      \
    }                                                                                                                  \
    V operator-(V a, V b) noexcept {                                                                                   \
        V r;                                                                                                           \
        for (std::size_t i = 0; i < V::component_count; ++i) r[i] = a[i] - b[i];                                       \
        return r;                                                                                                      \
    }                                                                                                                  \
    V operator-(V a) noexcept {                                                                                        \
        V r;                                                                                                           \
        for (std::size_t i = 0; i < V::component_count; ++i) r[i] = -a[i];                                             \
        return r;                                                                                                      \
    }                                                                                                                  \
    V operator*(V a, V b) noexcept {                                                                                   \
        V r;                                                                                                           \
        for (std::size_t i = 0; i < V::component_count; ++i) r[i] = a[i] * b[i];                                       \
        return r;                                                                                                      \
    }                                                                                                                  \
    V operator/(V a, V b) noexcept {                                                                                   \
        V r;                                                                                                           \
        for (std::size_t i = 0; i < V::component_count; ++i) r[i] = component_divide(a[i], b[i]);                      \
        return r;                                                                                                      \
    }                                                                                                                  \
    V operator*(V a, S s) noexcept {                                                                                   \
        V r;                                                                                                           \
        for (std::size_t i = 0; i < V::component_count; ++i) r[i] = a[i] * s;                                          \
        return r;                                                                                                      \
    }                                                                                                                  \
    V operator*(S s, V a) noexcept { return a * s; }                                                                   \
    V operator/(V a, S s) noexcept {                                                                                   \
        V r;                                                                                                           \
        for (std::size_t i = 0; i < V::component_count; ++i) r[i] = component_divide(a[i], s);                         \
        return r;                                                                                                      \
    }                                                                                                                  \
    V& operator+=(V& a, V b) noexcept { return a = a + b; }                                                            \
    V& operator-=(V& a, V b) noexcept { return a = a - b; }                                                            \
    V& operator*=(V& a, S s) noexcept { return a = a * s; }                                                            \
    V& operator/=(V& a, S s) noexcept { return a = a / s; }                                                            \
    bool operator==(V a, V b) noexcept {                                                                               \
        for (std::size_t i = 0; i < V::component_count; ++i)                                                           \
            if (a[i] != b[i]) return false;                                                                            \
        return true;                                                                                                   \
    }                                                                                                                  \
    bool operator!=(V a, V b) noexcept { return !(a == b); }                                                           \
    S dot(V a, V b) noexcept {                                                                                         \
        long double sum = 0;                                                                                           \
        for (std::size_t i = 0; i < V::component_count; ++i) sum += static_cast<long double>(a[i]) * b[i];             \
        return static_cast<S>(sum);                                                                                    \
    }                                                                                                                  \
    S length_squared(V v) noexcept { return dot(v, v); }                                                               \
    S length(V v) noexcept { return vector_length(v); }                                                                \
    V min(V a, V b) noexcept {                                                                                         \
        V r;                                                                                                           \
        for (std::size_t i = 0; i < V::component_count; ++i) r[i] = component_min(a[i], b[i]);                         \
        return r;                                                                                                      \
    }                                                                                                                  \
    V max(V a, V b) noexcept {                                                                                         \
        V r;                                                                                                           \
        for (std::size_t i = 0; i < V::component_count; ++i) r[i] = component_max(a[i], b[i]);                         \
        return r;                                                                                                      \
    }                                                                                                                  \
    bool is_finite(V v) noexcept { return finite_vector(v); }                                                          \
    bool nearly_equal(V a, V b, S tolerance) noexcept { return near_vector(a, b, tolerance); }                         \
    bool try_normalize(V v, V& out) noexcept { return normalize_vector(v, out); }                                      \
    V normalize(V v) noexcept {                                                                                        \
        V r;                                                                                                           \
        (void)try_normalize(v, r);                                                                                     \
        return r;                                                                                                      \
    }
AEG_VECTOR_IMPL(Vec2, float)
AEG_VECTOR_IMPL(Vec3, float)
AEG_VECTOR_IMPL(Vec4, float)
AEG_VECTOR_IMPL(DVec2, double)
AEG_VECTOR_IMPL(DVec3, double)
#undef AEG_VECTOR_IMPL
Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {static_cast<float>(static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y),
            static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z),
            static_cast<float>(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x)};
}
DVec3 cross(DVec3 a, DVec3 b) noexcept { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
} // namespace aeg::math
