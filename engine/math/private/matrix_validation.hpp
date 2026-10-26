#pragma once
#include "math.hpp"
namespace aeg::math::detail {
struct DoubleMatrixInverse {
    double elements[4][4]{};
};
// Validation does not require an inverse representable as a public float Mat4.
bool try_inverse_double(const Mat4& m, DoubleMatrixInverse& out, float relative_tolerance) noexcept;
bool is_invertible(const Mat4& m) noexcept;
}
