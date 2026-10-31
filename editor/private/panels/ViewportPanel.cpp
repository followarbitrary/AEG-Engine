#include "EditorState.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace aeg::editor {
namespace {
foundation::Bounds transformedBounds(const foundation::Bounds& bounds, const math::Mat4& transform) {
    foundation::Bounds result;
    result.min = math::Vec3(std::numeric_limits<float>::max());
    result.max = math::Vec3(std::numeric_limits<float>::lowest());
    for (unsigned corner = 0; corner < 8; ++corner) {
        const math::Vec3 point{corner & 1 ? bounds.max.x : bounds.min.x, corner & 2 ? bounds.max.y : bounds.min.y,
                               corner & 4 ? bounds.max.z : bounds.min.z};
        const auto world = math::transform_point(transform, point);
        result.min = math::min(result.min, world);
        result.max = math::max(result.max, world);
    }
    return result;
}

}

void drawViewportPanel(EditorState& state, render::Renderer& renderer, EditorGraphics& graphics) {
    auto& viewportWidth = state.viewportWidth;
    auto& viewportHeight = state.viewportHeight;
    auto& viewportDrag = state.viewportDrag;
    auto& camera = state.camera();
    auto& frameRequested = state.frameRequested;
    auto& sceneRendered = state.sceneRendered;
    auto& settings = state.settings;
    auto& transform = state.transform();
    auto& model = state.model();
    const auto& io = ImGui::GetIO();
    bool visible = false;
    for (int button = 0; button < 3; ++button)
        if (!ImGui::IsMouseDown(button)) viewportDrag[button] = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin(viewportWindow, nullptr,
                     workspacePanelFlags() | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const auto available = ImGui::GetContentRegionAvail();
        if (available.x >= 1 && available.y >= 1) {
            visible = true;
            viewportWidth =
                static_cast<std::uint32_t>(std::max(1.0f, std::floor(available.x * io.DisplayFramebufferScale.x)));
            viewportHeight =
                static_cast<std::uint32_t>(std::max(1.0f, std::floor(available.y * io.DisplayFramebufferScale.y)));
            renderer.resizeViewport(viewportWidth, viewportHeight);
            const auto imagePosition = ImGui::GetCursorScreenPos();
            ImGui::Image(graphics.viewportTexture(), available);
            if (frameRequested) {
                camera.fit(transformedBounds(model.bounds, transform.matrix()));
                renderer.resetTemporalHistory();
                frameRequested = false;
            }
            const ViewportRect rect{{imagePosition.x, imagePosition.y}, {available.x, available.y}};
            const bool cameraOwnsGesture =
                std::any_of(viewportDrag.begin(), viewportDrag.end(), [](bool v) { return v; });
            const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                                 rect.contains({io.MousePos.x, io.MousePos.y});
            const bool toolbarClaims = drawGizmoToolbar(state.gizmo, rect,
                                                        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                                                            !cameraOwnsGesture && !state.orientationGizmo.active);
            // Test the orientation widget first for input priority, but paint it
            // after model handles within this window's layer.
            auto* draw = ImGui::GetWindowDrawList();
            draw->ChannelsSplit(2);
            draw->ChannelsSetCurrent(1);
            const bool orientationClaims =
                drawOrientationGizmo(state.orientationGizmo, camera, rect,
                                     hovered && !cameraOwnsGesture && !state.gizmo.active &&
                                         !state.gizmo.blockedUntilRelease && !toolbarClaims && !io.WantTextInput);
            if (state.orientationGizmo.snapped) renderer.resetTemporalHistory();
            draw->ChannelsSetCurrent(0);
            const bool transformClaims = drawTransformGizmo(state.gizmo, transform, camera, model.bounds.center(), rect,
                                                            !cameraOwnsGesture && !orientationClaims && !toolbarClaims);
            draw->ChannelsMerge();
            const bool overlayClaims = toolbarClaims || orientationClaims || transformClaims;
            // Register the input item only during gestures. A passive hovered
            // item would make ImGuizmo reject the next click via IsAnyItemHovered.
            // During a gesture it prevents ImGui's window-move ID taking the mouse.
            bool inputAvailable = !ImGui::IsAnyItemActive();
            const bool mouseChanged = ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) ||
                                      ImGui::IsMouseClicked(2) || ImGui::IsMouseReleased(0) ||
                                      ImGui::IsMouseReleased(1) || ImGui::IsMouseReleased(2);
            ImGui::SetCursorScreenPos(imagePosition);
            if (cameraOwnsGesture || state.gizmo.active || state.gizmo.blockedUntilRelease ||
                state.orientationGizmo.active || mouseChanged) {
                ImGui::InvisibleButton("##ViewportInput", available,
                                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                           ImGuiButtonFlags_MouseButtonMiddle);
                inputAvailable = !ImGui::IsAnyItemActive() || ImGui::IsItemActive();
            }
            for (int button = 0; button < 3; ++button) {
                if (hovered && !overlayClaims && inputAvailable && !io.WantTextInput && ImGui::IsMouseClicked(button))
                    viewportDrag[button] = true;
            }
            if (inputAvailable && !overlayClaims && !io.WantTextInput) {
                if ((viewportDrag[ImGuiMouseButton_Left] && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) ||
                    (viewportDrag[ImGuiMouseButton_Right] && ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
                    camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
                if (viewportDrag[ImGuiMouseButton_Middle] && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                    camera.pan(io.MouseDelta.x, io.MouseDelta.y, available.y);
                if (hovered && io.MouseWheel != 0) camera.zoom(io.MouseWheel);
                if (hovered && ImGui::IsKeyPressed(ImGuiKey_F, false)) frameRequested = true;
            }
            const float hintHeight = ui::px(30.0f);
            const ImVec2 hintMin{imagePosition.x, imagePosition.y + available.y - hintHeight};
            const ImVec2 hintMax{imagePosition.x + available.x, imagePosition.y + available.y};
            draw->AddRectFilled(hintMin, hintMax, ImGui::GetColorU32(ui::rgb(0x0f1216, 0.86f)));
            draw->AddText({hintMin.x + ui::px(14.0f), hintMin.y + (hintHeight - ImGui::GetTextLineHeight()) * 0.5f},
                          ImGui::GetColorU32(ui::rgb(0x9aa3b0)),
                          "右键拖动 环绕 · 中键 平移 · 滚轮 缩放 · F 聚焦模型 · Esc 取消拖动");
            sceneRendered = true;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    if (!visible) {
        cancelTransformGizmo(state.gizmo, transform, true);
        state.orientationGizmo = {};
        viewportDrag = {};
    }
    (void)state.updateWorldTransforms();
    settings.model = state.worldMatrix();
    if (frameRequested) {
        camera.fit(transformedBounds(model.bounds, settings.model));
        renderer.resetTemporalHistory();
        frameRequested = false;
    }
    settings.set_camera(
        camera.render_camera(static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight), {true, false}));
}
}
