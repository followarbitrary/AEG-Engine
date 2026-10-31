#include "EditorState.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>

namespace aeg::editor {
namespace {
bool drawRightTab(const char* label, const bool active) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::px(5.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, active ? ui::rgb(0x1f3a4a) : ui::rgb(0x000000, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::rgb(0x20252d));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::rgb(0x2a303a));
    const bool clicked = ImGui::Button(label, {ui::px(72.0f), ui::px(32.0f)});
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    if (active) {
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled({min.x, max.y - ui::px(2.0f)}, {max.x, max.y},
                                                   ImGui::GetColorU32(ui::rgb(0x3fb4e6)));
    }
    return clicked;
}
}

void drawInspectorPanel(EditorState& state, render::Renderer& renderer, EditorGraphics& graphics) {
    auto& transform = state.transform();
    auto& autoRotate = state.autoRotate();
    auto& frameRequested = state.frameRequested;
    auto& supportedTaa = state.supportedTaa;
    auto& supportedMsaa = state.supportedMsaa;
    auto& antialiasing = state.antialiasing;
    auto& antialiasingNotice = state.antialiasingNotice;
    bool antialiasingChanged = false;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::rgb(0x1a1e25));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::rgb(0x1a1e25));
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (ImGui::Begin(inspectorWindow, nullptr, workspacePanelFlags())) {
        ui::PaintPanelBackground();
        if (drawRightTab("属性", !state.debugPanelActive)) state.debugPanelActive = false;
        ImGui::SameLine(0.0f, 2.0f);
        if (drawRightTab("调试", state.debugPanelActive)) state.debugPanelActive = true;
        ImGui::Separator();
        ImGui::BeginChild("##RightPanelContent", {0, 0}, false);
        if (state.debugPanelActive) {
            drawDebugContents(state, renderer, graphics);
        } else {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        if (ImGui::CollapsingHeader("变换###Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            float translation[]{transform.translation.x, transform.translation.y, transform.translation.z};
            float rotation[]{transform.rotation.x, transform.rotation.y, transform.rotation.z};
            float scale[]{transform.scale.x, transform.scale.y, transform.scale.z};
            if (ui::AxisField3("位置", translation, 0.02f))
                transform.translation = {translation[0], translation[1], translation[2]};
            if (ui::AxisField3("旋转", rotation, 0.5f))
                transform.rotation = {rotation[0], rotation[1], rotation[2]};
            if (ui::AxisField3("缩放", scale, 0.01f, -1000, 1000))
                transform.scale = {scale[0], scale[1], scale[2]};
            for (int axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(transform.translation[axis])) transform.translation[axis] = 0;
                if (!std::isfinite(transform.rotation[axis])) transform.rotation[axis] = 0;
                if (!std::isfinite(transform.scale[axis])) transform.scale[axis] = 1;
                if (std::abs(transform.scale[axis]) < 0.001f)
                    transform.scale[axis] = std::signbit(transform.scale[axis]) ? -0.001f : 0.001f;
            }
            ImGui::Checkbox("自动旋转###Auto rotate", &autoRotate);
            if (ui::SecondaryButton("重置变换###Reset transform")) {
                cancelTransformGizmo(state.gizmo, transform, false);
                transform = {};
                renderer.resetTemporalHistory();
                frameRequested = true;
            }
        }
        drawMaterialSection(state, renderer);
        ImGui::Spacing();
        if (ui::SectionHeader("渲染###Rendering", "可调参数")) {
            drawToneMappingSection(state, renderer);
            drawLightingSection(state, renderer);
            drawEnvironmentSection(state, renderer);
            drawShadowsSection(state, renderer);
            const auto aaSummary = antialiasing.taa ? std::string("TAA")
                                                    : std::to_string(antialiasing.msaaSamples) + "x";
            if (ui::SectionHeader("抗锯齿###Antialiasing", aaSummary.c_str())) {
                ImGui::BeginDisabled(!supportedTaa);
                antialiasingChanged |= ImGui::Checkbox("TAA 时域抗锯齿###TAA", &antialiasing.taa);
                ImGui::EndDisabled();
                if (!supportedTaa)
                    ImGui::TextWrapped("当前设备不支持 TAA 所需的纹理格式或独立附件混合。可使用 MSAA 或 FXAA。");
                constexpr std::array<std::uint32_t, 4> sampleCounts{1, 2, 4, 8};
                constexpr std::array<const char*, 4> sampleLabels{"关闭 (Off)", "2x", "4x", "8x"};
                const auto selected = std::find(sampleCounts.begin(), sampleCounts.end(), antialiasing.msaaSamples);
                const auto selectedIndex = static_cast<std::size_t>(selected - sampleCounts.begin());
                ImGui::BeginDisabled(antialiasing.taa);
                if (ImGui::BeginCombo("MSAA###MSAA", sampleLabels[std::min<std::size_t>(selectedIndex, sampleLabels.size() - 1)])) {
                    for (std::size_t i = 0; i < sampleCounts.size(); ++i) {
                        if ((supportedMsaa & sampleCounts[i]) == 0) continue;
                        const bool active = antialiasing.msaaSamples == sampleCounts[i];
                        if (ImGui::Selectable(sampleLabels[i], active) && !active) {
                            antialiasing.msaaSamples = sampleCounts[i];
                            antialiasingChanged = true;
                        }
                        if (active) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
                if (antialiasing.taa) ImGui::TextUnformatted("TAA 替代 MSAA，有效采样数为 1。");
                antialiasingChanged |= ImGui::Checkbox("FXAA 后处理###FXAA", &antialiasing.fxaa);
                ImGui::Text("有效采样数：%u", renderer.antialiasing().msaaSamples);
                if (antialiasing.taa) {
                    ImGui::Text("当前历史帧数：%u", renderer.temporalFrameCount());
                    if (ui::SecondaryButton("重置历史###Reset temporal history")) renderer.resetTemporalHistory();
                }
                if (!antialiasingNotice.empty()) ImGui::TextWrapped("%s", antialiasingNotice.c_str());
            }
        }
        ImGui::PopStyleVar(2);
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);

    // Changing AA can replace the viewport descriptor. Apply it before Image
    // records its texture ID, never after this callback has recorded the image.
    if (antialiasingChanged) {
        try {
            renderer.setAntialiasing(antialiasing);
            antialiasingNotice.clear();
        } catch (const std::exception& error) {
            antialiasingNotice = std::string("抗锯齿设置失败，已保留当前设置：") + error.what();
            std::cerr << antialiasingNotice << '\n';
        }
        antialiasing = renderer.antialiasing();
    }
}
}
