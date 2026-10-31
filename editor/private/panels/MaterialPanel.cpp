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
std::string materialLabel(const assets::MaterialData& material, const std::size_t index) {
    return std::to_string(index) + " · " + (material.name.empty() ? "未命名材质" : material.name);
}

void selectMaterial(EditorState& state, const std::size_t index) {
    state.selectedMaterial = index;
    state.normalMapPath.fill('\0');
    state.materialNormalMapClear = false;
    const auto& material = state.model().materials[index];
    state.materialAlphaMode = material.alphaMode;
    state.materialOpacity = material.baseColor.w;
    state.materialAlphaCutoff = material.alphaCutoff;
    state.materialMetallic = material.metallicFactor;
    state.materialRoughness = material.roughnessFactor;
    state.materialNotice.clear();
    state.materialError.clear();
    state.materialDirty = false;
    state.materialEditorInitialized = true;
}

void applyMaterial(EditorState& state, render::Renderer& renderer) {
    try {
        auto candidate = state.model();
        assets::setMaterialPbr(candidate, state.selectedMaterial, state.materialMetallic, state.materialRoughness);
        assets::setMaterialAlpha(candidate, state.selectedMaterial, state.materialAlphaMode, state.materialOpacity,
                                 state.materialAlphaCutoff);
        if (state.materialNormalMapClear) {
            assets::clearMaterialNormalMap(candidate, state.selectedMaterial);
        } else if (state.normalMapPath[0]) {
            assets::setMaterialNormalMap(candidate, state.selectedMaterial,
                                         foundation::pathFromUtf8(state.normalMapPath.data()));
        }
        renderer.uploadModel(candidate);
        state.model() = std::move(candidate);
        state.materialError.clear();
        state.materialNotice = "已应用当前材质修改。";
        state.materialDirty = false;
        state.materialNormalMapClear = false;
        state.normalMapPath.fill('\0');
    } catch (const std::exception& error) {
        state.materialNotice.clear();
        state.materialError = std::string("材质修改失败，已保留当前模型：") + error.what();
        std::cerr << state.materialError << '\n';
    }
}
}

void resetMaterialEditor(EditorState& state) {
    state.selectedMesh = 0;
    state.selectedMaterial = 0;
    state.materialEditorInitialized = false;
    state.materialDirty = false;
    state.materialNormalMapClear = false;
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
    if (state.model().materials.empty()) {
        if (ui::SectionHeader("材质###Materials", "无材质")) ImGui::TextUnformatted("当前模型没有可编辑材质。");
        return;
    }
    if (state.selectedMesh >= state.model().meshes.size()) state.selectedMesh = 0;
    const auto linkedMaterial = state.model().meshes.empty() ? 0u : state.model().meshes[state.selectedMesh].materialIndex;
    if (!state.materialEditorInitialized || state.selectedMaterial >= state.model().materials.size())
        selectMaterial(state, std::min<std::size_t>(linkedMaterial, state.model().materials.size() - 1));
    const auto& mesh = state.model().meshes.empty() ? assets::MeshData{} : state.model().meshes[state.selectedMesh];
    const auto title = std::string("材质 · ") + (mesh.name.empty() ? "当前网格" : mesh.name);
    if (!ui::SectionHeader((title + "###Materials").c_str(), state.materialDirty ? "未应用" : nullptr,
                           ImGuiTreeNodeFlags_DefaultOpen))
        return;

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
    if (ImGui::SliderFloat("金属度###Metallic", &state.materialMetallic, 0, 1, "%.2f")) state.materialDirty = true;
    if (ImGui::SliderFloat("粗糙度###Roughness", &state.materialRoughness, 0, 1, "%.2f")) state.materialDirty = true;

    const char* alphaModes[]{"不透明", "裁切", "半透明"};
    int alphaMode = static_cast<int>(state.materialAlphaMode);
    ImGui::TextUnformatted("透明模式");
    if (ui::SegmentedControl("Alpha mode", alphaModes, 3, alphaMode)) {
        state.materialAlphaMode = static_cast<assets::AlphaMode>(alphaMode);
        state.materialDirty = true;
    }
    ImGui::BeginDisabled(state.materialAlphaMode == assets::AlphaMode::Opaque);
    if (ImGui::SliderFloat("不透明度###Opacity", &state.materialOpacity, 0, 1, "%.2f")) state.materialDirty = true;
    ImGui::EndDisabled();
    if (state.materialAlphaMode == assets::AlphaMode::Mask &&
        ImGui::InputFloat("裁切阈值###Alpha cutoff", &state.materialAlphaCutoff, 0.01f, 0.1f, "%.3f"))
        state.materialDirty = true;

    ImGui::Separator();
    const auto& material = state.model().materials[state.selectedMaterial];
    if (material.hasNormalMap)
        ImGui::Text("法线贴图：%u × %u", material.normalTexture.width, material.normalTexture.height);
    else
        ImGui::TextUnformatted("法线贴图：未指定");
    ImGui::TextWrapped("选择 RGB 法线贴图，相对路径以模型目录为基准。");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##NormalMapPath", "法线贴图文件路径", state.normalMapPath.data(),
                                 state.normalMapPath.size(), ImGuiInputTextFlags_EnterReturnsTrue))
        state.materialDirty = true;
    ImGui::BeginDisabled(!material.hasNormalMap);
    if (ui::SecondaryButton("清除法线贴图###Clear normal map", {-1.0f, 0.0f})) {
        state.materialNormalMapClear = true;
        state.materialDirty = true;
    }
    ImGui::EndDisabled();
    if (state.materialDirty && ui::AccentButton("应用材质修改###Apply material", {-1.0f, 0.0f}))
        applyMaterial(state, renderer);
    if (!state.materialNotice.empty()) ImGui::TextWrapped("%s", state.materialNotice.c_str());
    if (!state.materialError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::rgb(0xe5645a));
        ImGui::TextWrapped("%s", state.materialError.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::TextDisabled("材质修改会在应用后上传到当前会话。");
}
}
