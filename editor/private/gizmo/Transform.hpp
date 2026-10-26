#pragma once
#include <math.hpp>

namespace aeg::editor {
struct Transform {
    math::Vec3 translation{0};
    math::Vec3 rotation{0};
    math::Vec3 scale{1};
    math::Mat4 matrix() const {
        const auto orientation = math::axis_angle({1, 0, 0}, math::to_radians(rotation.x)) *
                                 math::axis_angle({0, 1, 0}, math::to_radians(rotation.y)) *
                                 math::axis_angle({0, 0, 1}, math::to_radians(rotation.z));
        return math::trs(translation, orientation, scale);
    }
};

// The gizmo edits a matrix whose local origin is the selected model's pivot.
math::Mat4 pivotMatrix(const Transform& transform, math::Vec3 localPivot);
// Preserve reference scale signs and choose the nearest equivalent Rx*Ry*Rz
// Euler angles in degrees. Scale magnitudes must stay within [0.001, 1000].
// Invalid, sheared or degenerate inputs leave out unchanged.
bool tryApplyPivotMatrix(const math::Mat4& pivot, math::Vec3 localPivot, const Transform& reference, Transform& out);
}
