#pragma once
#include "math.hpp"
namespace aeg::math::detail {
Mat4 multiply(const Mat4& a, const Mat4& b) noexcept;
Vec4 multiply(const Mat4& m, Vec4 v) noexcept;
}
