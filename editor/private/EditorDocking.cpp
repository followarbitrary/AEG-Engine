#include "EditorState.hpp"
#include <imgui_internal.h>

namespace aeg::editor {
namespace {
void dockLayout(ImGuiID dockId) {
    if (ImGui::DockBuilderGetNode(dockId)) return;
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);
    auto center = dockId;
    const auto inspector = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
    auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.27f, nullptr, &center);
    const auto assets = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.57f, nullptr, &left);
    ImGui::DockBuilderDockWindow(viewportWindow, center);
    ImGui::DockBuilderDockWindow(sceneWindow, left);
    ImGui::DockBuilderDockWindow(importWindow, assets);
    ImGui::DockBuilderDockWindow(inspectorWindow, inspector);
    ImGui::DockBuilderDockWindow(debugWindow, inspector);
    ImGui::DockBuilderFinish(dockId);
}
}

void drawWorkspace(render::Renderer& renderer) {
    const auto& io = ImGui::GetIO();
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("AEG 工作区###AEG Workspace", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                     ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleVar();
    if (ImGui::BeginMenuBar()) {
        ImGui::TextColored({0.40f, 0.79f, 0.95f, 1}, "AEG");
        ImGui::TextUnformatted(" / 模型工作室");
        ImGui::Separator();
        ImGui::Text("%.0f FPS  |  %.2f ms", io.Framerate, io.Framerate > 0 ? 1000.0f / io.Framerate : 0.0f);
        ImGui::Separator();
        ImGui::TextUnformatted(renderer.deviceName().c_str());
        ImGui::EndMenuBar();
    }
    const auto dockId = ImGui::GetID("AEGDockSpace");
    dockLayout(dockId);
    ImGui::DockSpace(dockId);
    ImGui::End();
}
}
