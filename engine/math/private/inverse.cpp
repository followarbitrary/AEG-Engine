#include "math.hpp"
#include "matrix_validation.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
#include <array>
#include <limits>

namespace aeg::math {
namespace detail {
namespace {
bool has_reliable_determinant(const Mat4& m, std::size_t dimension) noexcept {
    std::array<std::size_t, 4> permutation{0, 1, 2, 3};
    double determinant = 0, absolute_terms = 0;
    do {
        double term = 1;
        unsigned inversions = 0;
        for (std::size_t r = 0; r < dimension; ++r) {
            term *= static_cast<double>(m.at(r, permutation[r]));
            for (std::size_t c = r + 1; c < dimension; ++c)
                if (permutation[r] > permutation[c]) ++inversions;
        }
        determinant += (inversions % 2 == 0) ? term : -term;
        absolute_terms += std::abs(term);
    } while (std::next_permutation(permutation.begin(), permutation.begin() + dimension));
    // Every product of up to four finite binary32 values fits the exponent
    // range of double. This relative cancellation bound covers the rounding
    // in the products and the sum of at most 24 terms, independently of units.
    constexpr double relative_roundoff = 64 * std::numeric_limits<double>::epsilon();
    return std::abs(determinant) > relative_roundoff * absolute_terms;
}
}
bool try_inverse_double(const Mat4& m, DoubleMatrixInverse& out, float tolerance) noexcept {
    if (!is_finite(m) || !std::isfinite(tolerance) || tolerance < 0) return false;
    const bool affine = m.at(3, 0) == 0 && m.at(3, 1) == 0 && m.at(3, 2) == 0 && m.at(3, 3) == 1;
    const std::size_t n = affine ? 3u : 4u;
    if (!has_reliable_determinant(m, n)) return false;
    double rows[4][8]{}, row_scale[4]{};
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            rows[r][c] = m.at(r, c);
            row_scale[r] = std::max(row_scale[r], std::abs(rows[r][c]));
        }
        if (row_scale[r] == 0) return false;
        rows[r][n + r] = 1;
    }
    for (std::size_t c = 0; c < n; ++c) {
        std::size_t pivot = c;
        double best = 0;
        for (std::size_t r = c; r < n; ++r) {
            const double ratio = std::abs(rows[r][c]) / row_scale[r];
            if (ratio > best) {
                best = ratio;
                pivot = r;
            }
        }
        if (best <= tolerance) return false;
        if (pivot != c) {
            for (std::size_t j = 0; j < 2 * n; ++j) std::swap(rows[c][j], rows[pivot][j]);
            std::swap(row_scale[c], row_scale[pivot]);
        }
        const double divisor = rows[c][c];
        if (divisor == 0) return false;
        for (std::size_t j = 0; j < 2 * n; ++j) rows[c][j] /= divisor;
        for (std::size_t r = 0; r < n; ++r)
            if (r != c) {
                const double factor = rows[r][c];
                for (std::size_t j = 0; j < 2 * n; ++j) rows[r][j] -= factor * rows[c][j];
            }
    }
    double matrix_norm = 0, inverse_norm = 0;
    for (std::size_t r = 0; r < n; ++r) {
        double matrix_sum = 0, inverse_sum = 0;
        for (std::size_t c = 0; c < n; ++c) {
            matrix_sum += std::abs(static_cast<double>(m.at(r, c)));
            inverse_sum += std::abs(rows[r][n + c]);
        }
        matrix_norm = std::max(matrix_norm, matrix_sum);
        inverse_norm = std::max(inverse_norm, inverse_sum);
    }
    const double reciprocal_condition = 1.0 / (matrix_norm * inverse_norm);
    if (!std::isfinite(reciprocal_condition) || reciprocal_condition <= tolerance) return false;
    DoubleMatrixInverse result;
    if (affine) result.elements[3][3] = 1;
    for (std::size_t r = 0; r < n; ++r)
        for (std::size_t c = 0; c < n; ++c) result.elements[r][c] = rows[r][n + c];
    if (affine)
        for (std::size_t r = 0; r < 3; ++r) {
            double t = 0;
            for (std::size_t c = 0; c < 3; ++c) t -= rows[r][n + c] * m.at(c, 3);
            result.elements[r][3] = t;
        }
    for (const auto& row : result.elements)
        for (double value : row)
            if (!std::isfinite(value)) return false;
    out = result;
    return true;
}
bool is_invertible(const Mat4& m) noexcept {
    DoubleMatrixInverse inverse;
    return try_inverse_double(m, inverse, 0);
}
} // namespace detail
bool try_inverse(const Mat4& m, Mat4& out, float tolerance) noexcept {
    detail::DoubleMatrixInverse inverse;
    if (!detail::try_inverse_double(m, inverse, tolerance)) return false;
    Mat4 result;
    for (std::size_t r = 0; r < 4; ++r)
        for (std::size_t c = 0; c < 4; ++c) result.at(r, c) = static_cast<float>(inverse.elements[r][c]);
    if (!is_finite(result)) return false;
    out = result;
    return true;
}
Mat4 inverse(const Mat4& m, float tolerance) noexcept {
    Mat4 result;
    (void)try_inverse(m, result, tolerance);
    return result;
}
} // namespace aeg::math
