#include "math.hpp"

// This translation unit deliberately includes only the public Math header.
static_assert(aeg::math::Vec3::component_count == 3);
static_assert(aeg::math::Mat4{}.at(0, 0) == 0);
static_assert(aeg::math::Quat{}.w == 1);
static_assert(aeg::math::Vec4(aeg::math::Vec3(1, 2, 3), 4).w == 4);
static_assert(aeg::math::Vec3(aeg::math::Vec4(1, 2, 3, 4)).z == 3);
