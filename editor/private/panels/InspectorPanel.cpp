#include "EditorState.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>

namespace aeg::editor {
namespace {
void drawInspectorHeading(const char* label, const char* detail = nullptr) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (detail) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", detail);
    }
    ImGui::Spacing();
}
}

void drawInspectorPanel(EditorState& state, render::Renderer& renderer) {
    auto& transform = state.transform();
    auto& autoRotate = state.autoRotate();
    auto& settings = state.settings;
    auto& frameRequested = state.frameRequested;
    auto& supportedTaa = state.supportedTaa;
    auto& supportedMsaa = state.supportedMsaa;
    auto& antialiasing = state.antialiasing;
    auto& antialiasingNotice = state.antialiasingNotice;
    auto& viewportWidth = state.viewportWidth;
    auto& viewportHeight = state.viewportHeight;
    bool antialiasingChanged = false;
    if (ImGui::Begin(inspectorWindow)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        drawInspectorHeading("对象属性", "Transform / Material / Lighting");
        if (ImGui::CollapsingHeader("变换###Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            float translation[]{transform.translation.x, transform.translation.y, transform.translation.z};
            float rotation[]{transform.rotation.x, transform.rotation.y, transform.rotation.z};
            float scale[]{transform.scale.x, transform.scale.y, transform.scale.z};
            if (ImGui::DragFloat3("位置###Position", translation, 0.02f))
                transform.translation = {translation[0], translation[1], translation[2]};
            if (ImGui::DragFloat3("旋转###Rotation", rotation, 0.5f))
                transform.rotation = {rotation[0], rotation[1], rotation[2]};
            if (ImGui::DragFloat3("缩放###Scale", scale, 0.01f, -1000, 1000))
                transform.scale = {scale[0], scale[1], scale[2]};
            for (int axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(transform.translation[axis])) transform.translation[axis] = 0;
                if (!std::isfinite(transform.rotation[axis])) transform.rotation[axis] = 0;
                if (!std::isfinite(transform.scale[axis])) transform.scale[axis] = 1;
                if (std::abs(transform.scale[axis]) < 0.001f)
                    transform.scale[axis] = std::signbit(transform.scale[axis]) ? -0.001f : 0.001f;
            }
            ImGui::Checkbox("自动旋转###Auto rotate", &autoRotate);
            if (ImGui::Button("重置变换###Reset transform")) {
                cancelTransformGizmo(state.gizmo, transform, false);
                transform = {};
                renderer.resetTemporalHistory();
                frameRequested = true;
            }
        }
        ImGui::Spacing();
        drawToneMappingSection(state, renderer);
        ImGui::Spacing();
        drawMaterialSection(state, renderer);
        ImGui::Spacing();
        drawLightingSection(state, renderer);
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("抗锯齿###Antialiasing", ImGuiTreeNodeFlags_DefaultOpen)) {
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
            if (ImGui::BeginCombo("MSAA###MSAA", sampleLabels[selectedIndex])) {
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
                if (ImGui::Button("重置历史###Reset temporal history")) renderer.resetTemporalHistory();
            }
            if (!antialiasingNotice.empty()) ImGui::TextWrapped("%s", antialiasingNotice.c_str());
        }
        drawInspectorHeading("渲染器", "Device / Validation / Viewport");
        ImGui::TextWrapped("Vulkan | %s", renderer.deviceName().c_str());
        ImGui::Text("验证错误：%u", renderer.validationErrorCount());
        ImGui::Text("视口：%u x %u", viewportWidth, viewportHeight);
        ImGui::PopStyleVar(2);
    }
    ImGui::End();

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
