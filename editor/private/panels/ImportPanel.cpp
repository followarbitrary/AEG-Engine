#include "EditorState.hpp"
#include <ModelImporter.hpp>
#include <Utf8Path.hpp>

#include <cstdio>
#include <exception>
#include <iostream>
#include <utility>

namespace aeg::editor {
void drawImportPanel(EditorState& state) {
    auto& model = state.model;
    auto& imports = state.imports;
    auto& importPath = state.importPath;
    auto& status = state.status;
    if (ImGui::Begin(importWindow)) {
        ImGui::TextDisabled("模型库");
        ImGui::TextWrapped("打开 OBJ、glTF、GLB 或 PMX 文件，也可将文件拖入窗口。");
        ImGui::SetNextItemWidth(-1);
        const bool enter = ImGui::InputTextWithHint("##ModelPath", "模型文件路径", importPath.data(), importPath.size(),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("导入模型###Import model", {-1, 0}) || enter) {
            if (importPath[0])
                imports.paths.push_back(foundation::pathFromUtf8(importPath.data()));
            else
                imports.error = "请先输入模型文件路径。";
        }
        ImGui::Separator();
        ImGui::TextWrapped("%s", status.c_str());
        if (!imports.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, {1, 0.49f, 0.43f, 1});
            ImGui::TextUnformatted("导入错误");
            ImGui::TextWrapped("%s", imports.error.c_str());
            ImGui::PopStyleColor();
        }
        if (!model.warnings.empty() && ImGui::CollapsingHeader("导入提示###Import notices"))
            for (const auto& warning : model.warnings) ImGui::TextWrapped("%s", warning.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("视口操作");
        ImGui::TextWrapped(
            "W / E / R：移动 / 旋转 / 缩放\n拖动手柄：变换模型\nEsc：取消本次拖动\n右上角方向轴：切换视角\n左键 / "
            "右键拖动空白处：环绕\n中键拖动：平移\n滚轮：缩放\nF：聚焦模型");
    }
    ImGui::End();
}

void processImports(EditorState& state, render::Renderer& renderer) {
    auto& model = state.model;
    auto& imports = state.imports;
    auto& importPath = state.importPath;
    auto& status = state.status;
    auto& transform = state.transform();
    auto& camera = state.camera;
    while (!imports.paths.empty()) {
        const auto path = std::move(imports.paths.front());
        imports.paths.pop_front();
        try {
            auto candidate = assets::loadModel(path);
            renderer.uploadModel(candidate);
            model = std::move(candidate);
            resetMaterialEditor(state);
            cancelTransformGizmo(state.gizmo, transform, false);
            state.orientationGizmo = {};
            state.viewportDrag = {};
            transform = {};
            // Each successful import starts from the front; ordinary focus keeps the orbit angle.
            camera = {};
            camera.fit(model.bounds);
            renderer.resetTemporalHistory();
            imports.error.clear();
            status = "已加载：" + model.name;
            const auto text = foundation::pathToUtf8(path);
            std::snprintf(importPath.data(), importPath.size(), "%s", text.c_str());
            std::cout << "Model: " << text << " (" << model.vertexCount() << " vertices, " << model.triangleCount()
                      << " triangles)\n";
        } catch (const std::exception& error) {
            imports.error = error.what();
            status = "导入失败，已保留当前模型。";
            std::cerr << "导入错误：" << error.what() << '\n';
        }
    }
}
}
