#include "EditorState.hpp"
#include <MaterialTextures.hpp>
#include <MaterialProperties.hpp>
#include <Utf8Path.hpp>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace aeg::editor {
namespace {
std::string materialLabel(const assets::MaterialData& material, std::size_t index) {
    return std::to_string(index) + " · " + (material.name.empty() ? "未命名材质" : material.name);
}

void selectMaterial(EditorState& state, std::size_t index) {
    state.selectedMaterial = index;
    state.normalMapPath.fill('\0');
    const auto& material = state.model().materials[index];
    state.materialAlphaMode = material.alphaMode;
    state.materialOpacity = material.baseColor.w;
    state.materialAlphaCutoff = material.alphaCutoff;
    state.materialMetallic = material.metallicFactor;
    state.materialRoughness = material.roughnessFactor;
    state.materialNotice.clear();
    state.materialError.clear();
    state.materialEditorInitialized = true;
}

void updateNormalMap(EditorState& state, render::Renderer& renderer, bool clear) {
    try {
        if (!clear && !state.normalMapPath[0]) throw std::runtime_error("请输入 RGB 法线贴图路径。");
        auto candidate = state.model();
        if (clear)
            assets::clearMaterialNormalMap(candidate, state.selectedMaterial);
        else
            assets::setMaterialNormalMap(candidate, state.selectedMaterial,
                                         foundation::pathFromUtf8(state.normalMapPath.data()));
        // Upload a complete candidate before changing the visible CPU model.
        // The renderer preserves its current model if replacement fails.
        std::string notice = clear ? "已清除所选材质的法线贴图。" : "已应用所选材质的法线贴图。";
        renderer.uploadModel(candidate);
        state.model() = std::move(candidate);
        state.materialError.clear();
        state.materialNotice = std::move(notice);
        if (clear) state.normalMapPath.fill('\0');
    } catch (const std::exception& error) {
        state.materialNotice.clear();
        state.materialError = std::string("材质修改失败，已保留当前模型：") + error.what();
        std::cerr << state.materialError << '\n';
    }
}

void updateAlpha(EditorState& state, render::Renderer& renderer) {
    try {
        auto candidate = state.model();
        assets::setMaterialAlpha(candidate, state.selectedMaterial, state.materialAlphaMode, state.materialOpacity,
                                 state.materialAlphaCutoff);
        std::string notice = "已应用所选材质的透明设置。";
        renderer.uploadModel(candidate);
        state.model() = std::move(candidate);
        state.materialNotice = std::move(notice);
        state.materialError.clear();
    } catch (const std::exception& error) {
        state.materialNotice.clear();
        state.materialError = std::string("材质修改失败，已保留当前模型：") + error.what();
        std::cerr << state.materialError << '\n';
    }
}

void updatePbr(EditorState& state, render::Renderer& renderer) {
    try {
        auto candidate = state.model();
        assets::setMaterialPbr(candidate, state.selectedMaterial, state.materialMetallic, state.materialRoughness);
        std::string notice = "已应用所选材质的 PBR 参数。";
        renderer.uploadModel(candidate);
        state.model() = std::move(candidate);
        state.materialNotice = std::move(notice);
        state.materialError.clear();
    } catch (const std::exception& error) {
        state.materialNotice.clear();
        state.materialError = std::string("材质修改失败，已保留当前模型：") + error.what();
        std::cerr << state.materialError << '\n';
    }
}
}

void resetMaterialEditor(EditorState& state) {
    state.selectedMaterial = 0;
    state.materialEditorInitialized = false;
    state.normalMapPath.fill('\0');
    state.materialAlphaMode = assets::AlphaMode::Opaque;
    state.materialOpacity = 1;
    state.materialAlphaCutoff = 0.5f;
    state.materialMetallic = 0;
    state.materialRoughness = 0.5f;
    state.materialNotice.clear();
    state.materialError.clear();
}

void drawMaterialSection(EditorState& state, render::Renderer& renderer) {
    if (!ImGui::CollapsingHeader("材质###Materials", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (state.model().materials.empty()) {
        ImGui::TextUnformatted("当前模型没有可编辑材质。");
        return;
    }
    if (!state.materialEditorInitialized || state.selectedMaterial >= state.model().materials.size())
        selectMaterial(state, 0);
    const auto preview = materialLabel(state.model().materials[state.selectedMaterial], state.selectedMaterial);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##Material selection", preview.c_str())) {
        for (std::size_t index = 0; index < state.model().materials.size(); ++index) {
            const auto label = materialLabel(state.model().materials[index], index);
            const bool active = state.selectedMaterial == index;
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(label.c_str(), active) && !active) selectMaterial(state, index);
            if (active) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted("PBR 材质");
    ImGui::SliderFloat("金属度###Metallic", &state.materialMetallic, 0, 1, "%.2f");
    ImGui::SliderFloat("粗糙度###Roughness", &state.materialRoughness, 0, 1, "%.2f");
    if (ImGui::Button("应用 PBR 参数###Apply PBR", {-1, 0})) updatePbr(state, renderer);
    ImGui::Separator();
    const char* alphaModes[]{"不透明", "透明裁切", "半透明"};
    auto alphaMode = static_cast<int>(state.materialAlphaMode);
    ImGui::TextUnformatted("透明模式");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##Alpha mode", &alphaMode, alphaModes, 3))
        state.materialAlphaMode = static_cast<assets::AlphaMode>(alphaMode);
    ImGui::BeginDisabled(state.materialAlphaMode == assets::AlphaMode::Opaque);
    ImGui::SliderFloat("不透明度###Opacity", &state.materialOpacity, 0, 1, "%.2f");
    ImGui::EndDisabled();
    if (state.materialAlphaMode == assets::AlphaMode::Mask)
        ImGui::InputFloat("裁切阈值###Alpha cutoff", &state.materialAlphaCutoff, 0.01f, 0.1f, "%.3f");
    if (state.materialAlphaMode == assets::AlphaMode::Opaque) ImGui::TextDisabled("不透明模式忽略图片和颜色的 Alpha。");
    if (ImGui::Button("应用透明设置###Apply transparency", {-1, 0})) updateAlpha(state, renderer);
    ImGui::Separator();
    const auto& material = state.model().materials[state.selectedMaterial];
    if (material.hasNormalMap) {
        ImGui::Text("法线贴图：%u × %u", material.normalTexture.width, material.normalTexture.height);
        ImGui::TextWrapped("来源：%s",
                           material.normalTextureSource.empty() ? "内存图片" : material.normalTextureSource.c_str());
    } else {
        ImGui::TextUnformatted("法线贴图：未指定");
    }
    ImGui::TextWrapped("选择 RGB 法线贴图，相对路径以模型目录为基准。");
    ImGui::SetNextItemWidth(-1);
    const bool enter = ImGui::InputTextWithHint("##NormalMapPath", "法线贴图文件路径", state.normalMapPath.data(),
                                                state.normalMapPath.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    const bool apply = ImGui::Button("应用法线贴图###Apply normal map", {-1, 0}) || enter;
    ImGui::BeginDisabled(!material.hasNormalMap);
    const bool clear = ImGui::Button("清除法线贴图###Clear normal map", {-1, 0});
    ImGui::EndDisabled();
    if (apply || clear) updateNormalMap(state, renderer, clear);
    if (!state.materialNotice.empty()) ImGui::TextWrapped("%s", state.materialNotice.c_str());
    if (!state.materialError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, {1, 0.49f, 0.43f, 1});
        ImGui::TextWrapped("%s", state.materialError.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::TextDisabled("修改仅在当前会话生效。");
}
}
