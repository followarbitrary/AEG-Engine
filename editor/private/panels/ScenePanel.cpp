#include "EditorState.hpp"

namespace aeg::editor {
namespace {
}

void drawScenePanel(EditorState& state) {
    auto& model = state.model();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::rgb(0x1a1e25));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::rgb(0x1a1e25));
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (ImGui::Begin(sceneWindow, nullptr, workspacePanelFlags())) {
        ui::PaintPanelBackground();
        ImGui::TextUnformatted("场景");
        ImGui::TextDisabled("%zu 网格 · %zu 材质 · %zu 顶点 · %zu 三角形", model.meshes.size(), model.materials.size(),
                            model.vertexCount(), model.triangleCount());
        if (state.renderableEntities.empty()) {
            ImGui::TextDisabled("暂无模型");
        } else {
            const auto root = state.renderableEntities.front();
            const auto modelName = model.name.empty() ? "未命名模型" : model.name.c_str();
            ImGui::PushID("SceneRoot");
            const bool rootSelected = state.selectedEntity == root;
            const auto rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
                                   (rootSelected ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);
            const bool open = ImGui::TreeNodeEx(modelName, rootFlags);
            if (ImGui::IsItemClicked()) state.selectedEntity = root;
            if (open) {
                if (state.selectedMesh >= model.meshes.size()) state.selectedMesh = 0;
                if (ImGui::BeginTable("SceneMeshes", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("三角形", ImGuiTableColumnFlags_WidthFixed, ui::px(52.0f));
                    for (std::size_t i = 0; i < model.meshes.size(); ++i) {
                        const auto& mesh = model.meshes[i];
                        const auto meshName = mesh.name.empty() ? "网格" : mesh.name.c_str();
                        ImGui::PushID(static_cast<int>(i));
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        const bool selected = state.selectedMesh == i;
                        ImGui::PushStyleColor(ImGuiCol_Header, ui::rgb(selected ? 0x1f3a4a : 0x20252d));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ui::rgb(0x2a5063));
                        if (ImGui::Selectable(meshName, selected, ImGuiSelectableFlags_SpanAllColumns)) {
                            state.selectedMesh = i;
                            state.selectedMaterial = std::min<std::size_t>(
                                mesh.materialIndex, model.materials.empty() ? 0 : model.materials.size() - 1);
                            state.materialEditorInitialized = false;
                            state.materialDirty = false;
                        }
                        ImGui::PopStyleColor(2);
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%zu", mesh.indices.size() / 3);
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}
}
