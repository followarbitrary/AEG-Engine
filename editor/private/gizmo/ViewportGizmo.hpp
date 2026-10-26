#pragma once
#include "Transform.hpp"
#include "ViewportRect.hpp"
#include <OrbitCamera.hpp>

namespace aeg::editor {
enum class GizmoOperation { Translate, Rotate, Scale };
enum class GizmoSpace { Local, World };

struct ViewportGizmoState {
    GizmoOperation operation{GizmoOperation::Translate};
    GizmoSpace space{GizmoSpace::Local};
    bool active{}, hovered{}, blockedUntilRelease{};
    Transform dragStart;
    math::Mat4 dragMatrix{math::identity()};
};

// Both functions draw in the current viewport window, in logical screen pixels.
bool drawGizmoToolbar(ViewportGizmoState& state, ViewportRect rect, bool allowShortcuts);
bool drawTransformGizmo(ViewportGizmoState& state, Transform& transform, const camera::OrbitCamera& camera,
                        math::Vec3 localPivot, ViewportRect rect, bool allowInput);
// Restore the initial value for Escape; import/reset can keep their replacement value.
void cancelTransformGizmo(ViewportGizmoState& state, Transform& transform, bool restore);
}
