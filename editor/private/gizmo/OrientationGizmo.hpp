#pragma once
#include <OrbitCamera.hpp>
#include <gizmo/ViewportRect.hpp>

namespace aeg::editor {
struct OrientationGizmoState {
    bool active{}, dragged{}, snapped{};
    int pressedAxis{-1};
    math::Vec2 pressPosition{};
    camera::OrbitCamera dragStart{};
};

// Draws in logical screen coordinates. Hover and an owned gesture consume input.
[[nodiscard]] bool drawOrientationGizmo(OrientationGizmoState& state, camera::OrbitCamera& camera,
                                        ViewportRect viewport, bool allowInput);
}
