#include <gizmo/OrientationGizmo.hpp>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace aeg::editor {
namespace {
constexpr std::array<math::Vec3, 6> directions{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
constexpr std::array<const char*, 6> labels{{"+X", "-X", "+Y", "-Y", "+Z", "-Z"}};
constexpr std::array<ImU32, 6> colors{{IM_COL32(235, 85, 87, 255), IM_COL32(142, 60, 63, 255),
                                       IM_COL32(106, 208, 102, 255), IM_COL32(57, 129, 62, 255),
                                       IM_COL32(84, 150, 241, 255), IM_COL32(49, 88, 152, 255)}};

ImVec2 screen(math::Vec2 value) { return {value.x, value.y}; }
float distanceSquared(math::Vec2 a, math::Vec2 b) {
    const auto delta = a - b;
    return math::dot(delta, delta);
}
void endGesture(OrientationGizmoState& state) {
    state.active = false;
    state.dragged = false;
    state.pressedAxis = -1;
}

// Gesture handling is independent of drawing: resizing or leaving the viewport
// cannot strand an owned mouse button or change the orbit input convention.
bool updateGesture(OrientationGizmoState& state, camera::OrbitCamera& camera) {
    if (!state.active) return false;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        camera = state.dragStart;
        endGesture(state);
        return true;
    }
    const auto& io = ImGui::GetIO();
    if (ImGui::IsMousePosValid()) {
        const math::Vec2 position{io.MousePos.x, io.MousePos.y};
        const float threshold = io.MouseDragThreshold;
        if (!state.dragged && distanceSquared(position, state.pressPosition) > threshold * threshold) {
            state.dragged = true;
            const auto delta = position - state.pressPosition;
            camera.orbit(delta.x, delta.y);
        } else if (state.dragged) {
            camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!state.dragged && state.pressedAxis >= 0 && state.pressedAxis < static_cast<int>(directions.size()))
            state.snapped = camera.setViewDirection(directions[static_cast<std::size_t>(state.pressedAxis)]);
        endGesture(state);
    }
    return true;
}
}

bool drawOrientationGizmo(OrientationGizmoState& state, camera::OrbitCamera& camera, ViewportRect viewport,
                          bool allowInput) {
    state.snapped = false;
    const bool ownedInput = updateGesture(state, camera);
    if (!math::is_finite(viewport.position) || !math::is_finite(viewport.size)) return ownedInput;
    const float size = std::min({124.0f, viewport.size.x - 24, viewport.size.y - 24});
    // Below this size six labeled endpoints no longer provide usable targets.
    if (size < 64) return ownedInput;

    const float scale = size / 124;
    const math::Vec2 center{viewport.position.x + viewport.size.x - 12 - size * 0.5f,
                            viewport.position.y + 12 + size * 0.5f};
    const float radius = 58 * scale, arm = 42 * scale, endpointRadius = 10 * scale;
    const auto& io = ImGui::GetIO();
    const math::Vec2 mouse{io.MousePos.x, io.MousePos.y};
    const bool hovered = allowInput && ImGui::IsMousePosValid() && viewport.contains(mouse) &&
                         distanceSquared(mouse, center) <= radius * radius;

    struct Endpoint {
        int axis;
        math::Vec2 position;
        math::Vec3 projected;
    };
    std::array<Endpoint, 6> endpoints;
    const auto view = camera.view();
    for (std::size_t index = 0; index < endpoints.size(); ++index) {
        const auto projected = math::transform_vector(view, directions[index]);
        // Camera space is Y-up; this is the only conversion to ImGui's Y-down.
        endpoints[index] = {static_cast<int>(index), center + math::Vec2{projected.x, -projected.y} * arm, projected};
    }
    std::stable_sort(endpoints.begin(), endpoints.end(),
                     [](const Endpoint& a, const Endpoint& b) { return a.projected.z < b.projected.z; });

    int hoveredAxis = -1;
    if (hovered) {
        // Reverse paint order gives the front circle ownership of overlaps.
        for (auto it = endpoints.rbegin(); it != endpoints.rend(); ++it) {
            if (distanceSquared(mouse, it->position) <= endpointRadius * endpointRadius) {
                hoveredAxis = it->axis;
                break;
            }
        }
    }
    if (hovered && !ownedInput && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state.active = true;
        state.dragged = false;
        state.pressedAxis = hoveredAxis;
        state.pressPosition = mouse;
        state.dragStart = camera;
        if (hoveredAxis >= 0) {
            const auto projected = math::transform_vector(view, directions[static_cast<std::size_t>(hoveredAxis)]);
            // At an exact axis view the two circles coincide. Clicking the
            // already aligned endpoint flips signs so the rear one is reachable.
            if (projected.z > 0 && projected.x * projected.x + projected.y * projected.y < 0.000001f)
                state.pressedAxis ^= 1;
        }
    }

    // Stay in the owner's layer so floating panels and popups cover the axes.
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(screen(viewport.position), screen(viewport.position + viewport.size), true);
    draw->AddCircleFilled(screen(center), radius, IM_COL32(20, 24, 31, hovered || state.active ? 215 : 170));
    draw->AddCircle(screen(center), radius, IM_COL32(116, 128, 148, 115));
    draw->AddCircleFilled(screen(center), 3 * scale, IM_COL32(165, 175, 190, 255));
    for (const auto& endpoint : endpoints) {
        const auto index = static_cast<std::size_t>(endpoint.axis);
        draw->AddLine(screen(center), screen(endpoint.position), colors[index], 2 * scale);
        draw->AddCircleFilled(screen(endpoint.position), endpointRadius, colors[index]);
        if (endpoint.axis == hoveredAxis)
            draw->AddCircle(screen(endpoint.position), endpointRadius + 1, IM_COL32_WHITE, 0, 1.5f);
        const float fontSize = 12 * scale;
        const auto textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, 100, 0, labels[index]);
        draw->AddText(ImGui::GetFont(), fontSize,
                      {endpoint.position.x - textSize.x * 0.5f, endpoint.position.y - textSize.y * 0.5f},
                      IM_COL32_WHITE, labels[index]);
    }
    draw->PopClipRect();
    if (hovered || state.active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    return ownedInput || hovered || state.active;
}
}
