#include "EditorState.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>

namespace aeg::editor {
void drawInspectorPanel(EditorState& state, render::Renderer& renderer) {
    auto& transform = state.transform;
    auto& autoRotate = state.autoRotate;
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
        if (ImGui::CollapsingHeader("变换###Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("位置###Position", &transform.translation.x, 0.02f);
            ImGui::DragFloat3("旋转###Rotation", &transform.rotation.x, 0.5f);
            ImGui::DragFloat3("缩放###Scale", &transform.scale.x, 0.01f, -1000, 1000);
            for (int axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(transform.translation[axis])) transform.translation[axis] = 0;
                if (!std::isfinite(transform.rotation[axis])) transform.rotation[axis] = 0;
                if (!std::isfinite(transform.scale[axis])) transform.scale[axis] = 1;
                if (std::abs(transform.scale[axis]) < 0.001f)
                    transform.scale[axis] = std::signbit(transform.scale[axis]) ? -0.001f : 0.001f;
            }
            ImGui::Checkbox("自动旋转###Auto rotate", &autoRotate);
            if (ImGui::Button("重置变换###Reset transform")) {
                transform = {};
                renderer.resetTemporalHistory();
                frameRequested = true;
            }
        }
        drawMaterialSection(state, renderer);
        if (ImGui::CollapsingHeader("光照###Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("无光照材质###Unlit materials", &settings.unlit);
            ImGui::BeginDisabled(settings.unlit);
            ImGui::Checkbox("使用法线贴图###Normal mapping", &settings.normalMapping);
            ImGui::BeginDisabled(!settings.normalMapping);
            ImGui::SliderFloat("法线强度###Normal strength", &settings.normalStrength, 0, 4, "%.2f");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            const auto normalMapMaterialCount = static_cast<std::size_t>(
                std::count_if(state.model.materials.begin(), state.model.materials.end(),
                              [](const assets::MaterialData& material) { return material.hasNormalMap; }));
            ImGui::Text("使用法线贴图的材质数：%zu", normalMapMaterialCount);
            ImGui::SliderFloat("环境光###Ambient", &settings.ambient, 0, 1, "%.2f");
            ImGui::TextUnformatted("光照方向");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat3("##LightDirection", &settings.lightDirection.x, -1, 1, "%.2f");
            if (glm::dot(settings.lightDirection, settings.lightDirection) < 0.0001f)
                settings.lightDirection = {0, -1, 0};
            ImGui::ColorEdit3("背景###Background", &settings.background.x);
        }
        if (ImGui::CollapsingHeader("抗锯齿###Antialiasing", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginDisabled(!supportedTaa);
            antialiasingChanged |= ImGui::Checkbox("TAA 时域抗锯齿###TAA", &antialiasing.taa);
            ImGui::EndDisabled();
            if (!supportedTaa) ImGui::TextWrapped("当前设备不支持 TAA 所需的纹理格式。可使用 MSAA 或 FXAA。");
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
        ImGui::Spacing();
        ImGui::TextDisabled("渲染器");
        ImGui::TextWrapped("Vulkan | %s", renderer.deviceName().c_str());
        ImGui::Text("验证错误：%u", renderer.validationErrorCount());
        ImGui::Text("视口：%u x %u", viewportWidth, viewportHeight);
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
