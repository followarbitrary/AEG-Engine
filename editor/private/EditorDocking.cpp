#include "EditorState.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <string>

namespace aeg::editor {
namespace {
void dockLayout(const ImGuiID dockId) {
    if (ImGui::DockBuilderGetNode(dockId)) return;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, viewport->WorkSize);
    auto center = dockId;
    const float dpi = ui::dpi();
    const float width = std::max(1.0f, viewport->WorkSize.x);
    const float rightWidth = 320.0f * dpi;
    const auto inspector = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right,
                                                       std::clamp(rightWidth / width, 0.16f, 0.32f), nullptr,
                                                       &center);
    const float leftBase = std::max(1.0f, width - rightWidth);
    auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,
                                            std::clamp(264.0f * dpi / leftBase, 0.18f, 0.34f), nullptr, &center);
    const auto assets = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.30f, nullptr, &left);
    ImGui::DockBuilderDockWindow(viewportWindow, center);
    ImGui::DockBuilderDockWindow(sceneWindow, left);
    ImGui::DockBuilderDockWindow(importWindow, assets);
    ImGui::DockBuilderDockWindow(inspectorWindow, inspector);
    ImGui::DockBuilderFinish(dockId);
    for (const auto node : {center, left, assets, inspector})
        if (auto* dockNode = ImGui::DockBuilderGetNode(node)) dockNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
}

void drawStatusDot(const ImVec4 color) {
    auto* draw = ImGui::GetWindowDrawList();
    const auto cursor = ImGui::GetCursorScreenPos();
    const ImVec2 center{cursor.x + ui::px(4.0f), cursor.y + ImGui::GetTextLineHeight() * 0.5f};
    draw->AddCircleFilled(center, ui::px(4.0f), ImGui::GetColorU32(color));
    ImGui::Dummy({ui::px(10.0f), ImGui::GetTextLineHeight()});
}
}

ImGuiWindowFlags workspacePanelFlags() { return ImGuiWindowFlags_NoCollapse; }

void drawWorkspace(EditorState& state, render::Renderer& renderer) {
    const auto& io = ImGui::GetIO();
    const auto* viewport = ImGui::GetMainViewport();
    const auto statusHeight = ImGui::GetTextLineHeight() + ui::px(12.0f);
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::rgb(0x14171c));
    ImGui::Begin("AEG 工作区###AEG Workspace", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                     ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (ImGui::BeginMenuBar()) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ui::px(20.0f), 0.0f});
        ImGui::PushStyleColor(ImGuiCol_Text, ui::rgb(0x3fb4e6));
        ImGui::TextUnformatted("AEG");
        ImGui::PopStyleColor();
        for (const auto* label : {"文件", "编辑", "视图", "窗口", "帮助"}) {
            ImGui::SameLine();
            ImGui::TextUnformatted(label);
        }
        ImGui::PopStyleVar();

        const auto modelName = state.model().name.empty() ? "未命名模型" : state.model().name.c_str();
        const auto title = std::string("模型工作室 · ") + modelName;
        const auto titleWidth = ImGui::CalcTextSize(title.c_str()).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + ui::px(20.0f),
                                 (ImGui::GetWindowWidth() - titleWidth) * 0.5f));
        ImGui::TextDisabled("%s", title.c_str());

        const auto metrics = std::string("%.0f FPS · %.2f ms · ") + renderer.deviceName();
        const auto metricsWidth = ImGui::CalcTextSize(metrics.c_str()).x + ui::px(20.0f);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + ui::px(24.0f), ImGui::GetWindowWidth() - metricsWidth));
        ImGui::TextDisabled("%.0f FPS · %.2f ms · %s", io.Framerate,
                            io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f, renderer.deviceName().c_str());
        ImGui::SameLine();
        drawStatusDot(ui::rgb(0x3fb4e6));
        ImGui::EndMenuBar();
    }

    const auto dockId = ImGui::GetID("AEGDockSpace");
    dockLayout(dockId);
    ImGui::DockSpace(dockId, {0.0f, -statusHeight}, ImGuiDockNodeFlags_None);
    ImGui::BeginChild("###AEGStatusBar", {0.0f, statusHeight}, true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawStatusDot(ui::rgb(0x9aa3b0));
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextDisabled("就绪 · 将模型拖入窗口即可导入");
    const auto& model = state.model();
    const auto status = std::string(model.name.empty() ? "未命名模型" : model.name) + " · " +
                        std::to_string(model.triangleCount()) + " 三角形 · 世界单位 m";
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + ui::px(24.0f),
                             ImGui::GetWindowWidth() - ImGui::CalcTextSize(status.c_str()).x - ui::px(20.0f)));
    ImGui::TextDisabled("%s", status.c_str());
    ImGui::EndChild();
    ImGui::End();
}
}
