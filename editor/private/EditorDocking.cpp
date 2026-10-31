#include "EditorState.hpp"
#include <imgui_internal.h>

namespace aeg::editor {
namespace {
void dockLayout(ImGuiID dockId) {
    if (ImGui::DockBuilderGetNode(dockId)) return;
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);
    auto center = dockId;
    // Reserve most of the default 1280px width for the viewport while keeping
    // both workspaces wide enough for their labels and controls.
    const auto inspector = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.23f, nullptr, &center);
    auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.23f, nullptr, &center);
    const auto assets = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.46f, nullptr, &left);
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
        ImGui::TextUnformatted("  模型工作室");
        ImGui::Separator();
        ImGui::TextUnformatted("文件");
        ImGui::TextUnformatted("编辑");
        ImGui::TextUnformatted("视图");
        ImGui::TextUnformatted("帮助");
        ImGui::Separator();
        ImGui::TextUnformatted("模型工作区");
        ImGui::SameLine();
        ImGui::Text("%.0f FPS  |  %.2f ms", io.Framerate, io.Framerate > 0 ? 1000.0f / io.Framerate : 0.0f);
        ImGui::Separator();
        ImGui::Text("GPU: %s", renderer.deviceName().c_str());
        ImGui::EndMenuBar();
    }
    const auto dockId = ImGui::GetID("AEGDockSpace");
    dockLayout(dockId);
    const auto& style = ImGui::GetStyle();
    // Reserve the child content height, including its window padding and both
    // border edges, then leave the parent item gap before the status child.
    const float statusBarHeight = ImGui::GetTextLineHeight() + (2.0f * style.WindowPadding.y) +
                                  (2.0f * style.ChildBorderSize);
    const float dockSpaceHeight = statusBarHeight + style.ItemSpacing.y;
    ImGui::DockSpace(dockId, {0, -dockSpaceHeight}, ImGuiDockNodeFlags_None);
    ImGui::BeginChild("###AEGStatusBar", {0, statusBarHeight}, true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextDisabled("就绪  ·  拖入模型以导入  ·  鼠标左键旋转  ·  滚轮缩放  ·  F 聚焦选中对象");
    ImGui::EndChild();
    ImGui::End();
}
}
