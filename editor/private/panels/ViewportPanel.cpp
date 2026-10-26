#include "EditorState.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace aeg::editor {
namespace {
foundation::Bounds transformedBounds(const foundation::Bounds& bounds, const glm::mat4& transform) {
    foundation::Bounds result;
    result.min = glm::vec3(std::numeric_limits<float>::max());
    result.max = glm::vec3(std::numeric_limits<float>::lowest());
    for (unsigned corner = 0; corner < 8; ++corner) {
        const glm::vec3 point{corner & 1 ? bounds.max.x : bounds.min.x, corner & 2 ? bounds.max.y : bounds.min.y,
                              corner & 4 ? bounds.max.z : bounds.min.z};
        const auto world = glm::vec3(transform * glm::vec4(point, 1));
        result.min = glm::min(result.min, world);
        result.max = glm::max(result.max, world);
    }
    return result;
}

}

void drawViewportPanel(EditorState& state, render::Renderer& renderer, EditorGraphics& graphics) {
    auto& viewportWidth = state.viewportWidth;
    auto& viewportHeight = state.viewportHeight;
    auto& viewportDrag = state.viewportDrag;
    auto& camera = state.camera;
    auto& frameRequested = state.frameRequested;
    auto& sceneRendered = state.sceneRendered;
    auto& settings = state.settings;
    auto& transform = state.transform;
    auto& model = state.model;
    const auto& io = ImGui::GetIO();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin(viewportWindow, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const auto available = ImGui::GetContentRegionAvail();
        if (available.x >= 1 && available.y >= 1) {
            viewportWidth =
                static_cast<std::uint32_t>(std::max(1.0f, std::floor(available.x * io.DisplayFramebufferScale.x)));
            viewportHeight =
                static_cast<std::uint32_t>(std::max(1.0f, std::floor(available.y * io.DisplayFramebufferScale.y)));
            renderer.resizeViewport(viewportWidth, viewportHeight);
            const auto imagePosition = ImGui::GetCursorScreenPos();
            ImGui::Image(graphics.viewportTexture(), available);
            // Image has no active ID. Own the gesture so ImGui's window-move
            // ID cannot consume drags over the scene, while other widgets
            // still block camera input.
            ImGui::SetCursorScreenPos(imagePosition);
            ImGui::InvisibleButton("##ViewportInput", available,
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                       ImGuiButtonFlags_MouseButtonMiddle);
            const bool inputAvailable = !ImGui::IsAnyItemActive() || ImGui::IsItemActive();
            for (int button = 0; button < 3; ++button) {
                if (!ImGui::IsMouseDown(button)) viewportDrag[button] = false;
                if (ImGui::IsItemHovered() && inputAvailable && ImGui::IsMouseClicked(button))
                    viewportDrag[button] = true;
            }
            if (ImGui::IsItemHovered() && inputAvailable && !io.WantTextInput) {
                if ((viewportDrag[ImGuiMouseButton_Left] && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) ||
                    (viewportDrag[ImGuiMouseButton_Right] && ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
                    camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
                if (viewportDrag[ImGuiMouseButton_Middle] && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                    camera.pan(io.MouseDelta.x, io.MouseDelta.y, available.y);
                if (io.MouseWheel != 0) camera.zoom(io.MouseWheel);
                if (ImGui::IsKeyPressed(ImGuiKey_F, false)) frameRequested = true;
            }
            sceneRendered = true;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    settings.model = transform.matrix();
    if (frameRequested) {
        camera.fit(transformedBounds(model.bounds, settings.model));
        renderer.resetTemporalHistory();
        frameRequested = false;
    }
    settings.viewProjection =
        camera.projection(static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight)) * camera.view();
}
}
