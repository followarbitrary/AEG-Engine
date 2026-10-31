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
    auto& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ui::rgb(0x1a1e25);
    style.Colors[ImGuiCol_ChildBg] = ui::rgb(0x14171c);
    style.Colors[ImGuiCol_DockingEmptyBg] = ui::rgb(0x14171c);
    const auto statusHeight = ImGui::GetTextLineHeight() + ui::px(12.0f);
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::rgb(0x14171c));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ui::rgb(0x0f1216));
    ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ui::rgb(0x14171c));
    ImGui::Begin("AEG 工作区###AEG Workspace", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                     ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    // Keep the shell header visually distinct from the docked panels. The
    // extra vertical padding gives the top bar the 36 px rhythm from the
    // reference layout while remaining DPI-scaled through ui::px().
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ui::px(8.0f), ui::px(8.0f)});
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
    ImGui::PopStyleVar();

    const auto dockId = ImGui::GetID("AEGDockSpace");
    dockLayout(dockId);
    ImGui::DockSpace(dockId, {0.0f, -statusHeight}, ImGuiDockNodeFlags_None);
    ImGui::BeginChild("###AEGStatusBar", {0.0f, statusHeight}, true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const bool hasImportError = !state.imports.error.empty();
    drawStatusDot(hasImportError ? ui::rgb(0xe5645a) : ui::rgb(0x9aa3b0));
    ImGui::SameLine(0.0f, 0.0f);
    const auto statusMessage = hasImportError ? state.status + " · " + state.imports.error : state.status;
    if (hasImportError) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::rgb(0xe5645a));
        ImGui::TextWrapped("%s", statusMessage.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("%s", statusMessage.c_str());
    }
    const auto& model = state.model();
    std::string selectedInfo = model.name.empty() ? "未命名模型" : model.name;
    if (!model.meshes.empty()) {
        const auto index = std::min(state.selectedMesh, model.meshes.size() - 1);
        const auto& mesh = model.meshes[index];
        selectedInfo = (mesh.name.empty() ? selectedInfo : mesh.name) + " · " +
                       std::to_string(mesh.indices.size() / 3) + " 三角形 · 世界单位 m";
    } else {
        selectedInfo += " · " + std::to_string(model.triangleCount()) + " 三角形 · 世界单位 m";
    }
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + ui::px(24.0f),
                             ImGui::GetWindowWidth() - ImGui::CalcTextSize(selectedInfo.c_str()).x - ui::px(20.0f)));
    ImGui::TextDisabled("%s", selectedInfo.c_str());
    ImGui::EndChild();
    ImGui::End();
}
}
