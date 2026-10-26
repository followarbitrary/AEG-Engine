#include "simd.hpp"
#if !defined(AEG_MATH_FORCE_SCALAR) &&                                                                                 \
    (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#define AEG_MATH_USE_SSE2 1
#include <emmintrin.h>
#endif

namespace aeg::math::detail {
Vec4 multiply(const Mat4& m, Vec4 v) noexcept {
#if defined(AEG_MATH_USE_SSE2)
    const __m128 a = _mm_mul_ps(_mm_loadu_ps(&m[0].x), _mm_set1_ps(v.x));
    const __m128 b = _mm_mul_ps(_mm_loadu_ps(&m[1].x), _mm_set1_ps(v.y));
    const __m128 c = _mm_mul_ps(_mm_loadu_ps(&m[2].x), _mm_set1_ps(v.z));
    const __m128 d = _mm_mul_ps(_mm_loadu_ps(&m[3].x), _mm_set1_ps(v.w));
    Vec4 out;
    _mm_storeu_ps(&out.x, _mm_add_ps(_mm_add_ps(_mm_add_ps(a, b), c), d));
    return out;
#else
    Vec4 out;
    for (std::size_t r = 0; r < 4; ++r)
        out[r] = ((m.at(r, 0) * v.x + m.at(r, 1) * v.y) + m.at(r, 2) * v.z) + m.at(r, 3) * v.w;
    return out;
#endif
}
Mat4 multiply(const Mat4& a, const Mat4& b) noexcept {
    return {multiply(a, b[0]), multiply(a, b[1]), multiply(a, b[2]), multiply(a, b[3])};
}
} // namespace aeg::math::detail
