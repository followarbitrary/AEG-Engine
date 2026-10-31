#include "EditorState.hpp"

namespace aeg::editor {
namespace {
void drawSectionHeading(const char* label) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("%s", label);
}

void drawEntityNode(EditorState& state, const ecs::Entity entity) {
    auto& registry = state.scene.registry();
    const auto& renderable = registry.get<Renderable>(entity);
    const std::string fallback = "实体 " + std::to_string(static_cast<std::uint32_t>(entity.raw()));
    const char* name = renderable.model.name.empty() ? fallback.c_str() : renderable.model.name.c_str();
    const bool hasChildren = std::ranges::any_of(state.renderableEntities, [&](const auto child) {
        return registry.all_of<Parent>(child) && registry.get<Parent>(child).entity == entity;
    });
    ImGui::PushID(static_cast<int>(entity.raw()));
    const bool selected = state.selectedEntity == entity;
    ImGuiTreeNodeFlags flags = selected ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    const bool open = ImGui::TreeNodeEx(name, flags);
    if (ImGui::IsItemClicked()) state.selectedEntity = entity;
    if (open) {
        for (const auto child : state.renderableEntities) {
            if (!registry.all_of<Parent>(child) || registry.get<Parent>(child).entity != entity) continue;
            drawEntityNode(state, child);
        }
        if (hasChildren) ImGui::TreePop();
    }
    ImGui::PopID();
}
}

void drawScenePanel(EditorState& state) {
    auto& model = state.model();
    auto& frameRequested = state.frameRequested;
    if (ImGui::Begin(sceneWindow)) {
        ImGui::TextDisabled("当前模型");
        ImGui::Selectable(model.name.c_str(), true);
        drawSectionHeading("场景树");
        for (const auto entity : state.renderableEntities) {
            if (state.scene.registry().all_of<Parent>(entity)) {
                const auto parent = state.scene.registry().get<Parent>(entity).entity;
                if (parent.raw() != entt::null &&
                    std::ranges::find(state.renderableEntities, parent) != state.renderableEntities.end())
                    continue;
            }
            drawEntityNode(state, entity);
        }
        drawSectionHeading("场景统计");
        if (ImGui::BeginTable("SceneStats", 2, ImGuiTableFlags_SizingStretchProp)) {
            const auto stat = [](const char* label, const std::size_t value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", label);
                ImGui::TableNextColumn();
                ImGui::Text("%zu", value);
            };
            stat("网格", model.meshes.size());
            stat("材质", model.materials.size());
            stat("顶点", model.vertexCount());
            stat("三角形", model.triangleCount());
            ImGui::EndTable();
        }
        if (ImGui::Button("聚焦模型###Frame model", {-1, 0})) frameRequested = true;
        drawSectionHeading("网格列表");
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
