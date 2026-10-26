#pragma once
#include <math.hpp>

namespace aeg::editor {
// Logical ImGui screen coordinates; framebuffer scale is handled by the renderer.
struct ViewportRect {
    math::Vec2 position;
    math::Vec2 size;
    [[nodiscard]] bool contains(math::Vec2 point) const {
        return point.x >= position.x && point.y >= position.y && point.x < position.x + size.x &&
               point.y < position.y + size.y;
    }
};
}
