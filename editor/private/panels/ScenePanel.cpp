#include "EditorState.hpp"

namespace aeg::editor {
void drawScenePanel(EditorState& state) {
    auto& model = state.model;
    auto& frameRequested = state.frameRequested;
    if (ImGui::Begin(sceneWindow)) {
        ImGui::TextDisabled("当前模型");
        ImGui::Selectable(model.name.c_str(), true);
        ImGui::Separator();
        ImGui::Text("网格：%zu", model.meshes.size());
        ImGui::Text("材质：%zu", model.materials.size());
        ImGui::Text("顶点：%zu", model.vertexCount());
        ImGui::Text("三角形：%zu", model.triangleCount());
        if (ImGui::Button("聚焦模型###Frame model", {-1, 0})) frameRequested = true;
        ImGui::Spacing();
        ImGui::TextDisabled("网格列表");
        if (ImGui::BeginTable("MeshList", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("三角形", ImGuiTableColumnFlags_WidthFixed, 72);
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < model.meshes.size(); ++i) {
                const auto& mesh = model.meshes[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(mesh.name.empty() ? "网格" : mesh.name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%zu", mesh.indices.size() / 3);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}
}
