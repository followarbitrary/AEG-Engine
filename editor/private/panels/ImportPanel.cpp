#include "EditorState.hpp"
#include <ModelImporter.hpp>
#include <Utf8Path.hpp>

#include <cstdio>
#include <algorithm>
#include <exception>
#include <iostream>
#include <utility>
#include <GLFW/glfw3.h>

namespace aeg::editor {
namespace {
void drawSectionHeading(const char* label) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("%s", label);
}

void drawDropZone(const ImVec2 size) {
    const auto min = ImGui::GetCursorScreenPos();
    const auto max = ImVec2(min.x + size.x, min.y + size.y);
    auto* draw = ImGui::GetWindowDrawList();
    const auto color = ImGui::GetColorU32(ImVec4{0.23f, 0.30f, 0.38f, 1.0f});
    for (float x = min.x + 2.0f; x < max.x - 2.0f; x += 12.0f)
        draw->AddLine({x, min.y}, {std::min(x + 7.0f, max.x - 2.0f), min.y}, color, 1.0f);
    for (float x = min.x + 2.0f; x < max.x - 2.0f; x += 12.0f)
        draw->AddLine({x, max.y}, {std::min(x + 7.0f, max.x - 2.0f), max.y}, color, 1.0f);
    for (float y = min.y + 2.0f; y < max.y - 2.0f; y += 12.0f)
        draw->AddLine({min.x, y}, {min.x, std::min(y + 7.0f, max.y - 2.0f)}, color, 1.0f);
    for (float y = min.y + 2.0f; y < max.y - 2.0f; y += 12.0f)
        draw->AddLine({max.x, y}, {max.x, std::min(y + 7.0f, max.y - 2.0f)}, color, 1.0f);
    const auto label = "拖入 OBJ / glTF / GLB / PMX";
    const auto labelSize = ImGui::CalcTextSize(label);
    ImGui::SetCursorScreenPos({min.x + (size.x - labelSize.x) * 0.5f,
                               min.y + (size.y - labelSize.y) * 0.5f});
    ImGui::TextDisabled("%s", label);
    ImGui::SetCursorScreenPos({min.x, max.y});
}
}

void drawImportPanel(EditorState& state) {
    auto& imports = state.imports;
    auto& importPath = state.importPath;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::rgb(0x1a1e25));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::rgb(0x1a1e25));
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (ImGui::Begin(importWindow, nullptr, workspacePanelFlags())) {
        ui::PaintPanelBackground();
        ImGui::TextUnformatted("导入");
        ImGui::Separator();
        if (ui::AccentButton("浏览模型…###Import model", {-1.0f, ui::px(34.0f)})) {
            if (const auto path = browseModelFile(glfwGetCurrentContext()); !path.empty()) {
                imports.paths.push_back(path);
                const auto text = foundation::pathToUtf8(path);
                std::snprintf(importPath.data(), importPath.size(), "%s", text.c_str());
            }
        }
        ImGui::Dummy({0.0f, 3.0f});
        drawDropZone({ImGui::GetContentRegionAvail().x, 48.0f});
        drawSectionHeading("最近打开");
        if (state.recentFiles.empty()) {
            ImGui::TextDisabled("暂无最近文件");
        } else {
            for (std::size_t index = 0; index < state.recentFiles.size(); ++index) {
                const auto& recent = state.recentFiles[index];
                const auto label = foundation::pathToUtf8(recent.filename());
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(label.c_str(), false)) imports.paths.push_back(recent);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", foundation::pathToUtf8(recent).c_str());
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

void processImports(EditorState& state, render::Renderer& renderer, assets::ModelResources& resources) {
    auto& model = state.model();
    auto& imports = state.imports;
    auto& importPath = state.importPath;
    auto& status = state.status;
    auto& transform = state.transform();
    auto& camera = state.camera();
    while (!imports.paths.empty()) {
        const auto path = std::move(imports.paths.front());
        imports.paths.pop_front();
        const auto handle = resources.request(path);
        const auto serial = ++imports.nextSerial;
        imports.latestSerial = serial;
        imports.serials[handle] = serial;
        imports.pending.push_back(handle);
        imports.error.clear();
        status = "后台加载中…";
    }
    for (auto& completion : resources.drainCompleted()) {
        imports.pending.erase(std::remove(imports.pending.begin(), imports.pending.end(), completion.handle),
                              imports.pending.end());
        const auto found = imports.serials.find(completion.handle);
        if (found == imports.serials.end() || found->second != imports.latestSerial) continue;
        if (completion.state == assets::ModelLoadState::Failed) {
            imports.error = completion.error;
            status = "导入失败，已保留当前模型。";
            continue;
        }
        if (completion.state != assets::ModelLoadState::Ready || !completion.data) continue;
        try {
            auto candidate = *completion.data;
            renderer.uploadModel(candidate);
            model = std::move(candidate);
            resetMaterialEditor(state);
            cancelTransformGizmo(state.gizmo, transform, false);
            state.orientationGizmo = {};
            state.viewportDrag = {};
            transform = {};
            camera = {};
            camera.fit(model.bounds);
            renderer.resetTemporalHistory();
            imports.error.clear();
            status = "已加载：" + model.name;
            const auto text = foundation::pathToUtf8(completion.path);
            std::snprintf(importPath.data(), importPath.size(), "%s", text.c_str());
            state.rememberRecentFile(completion.path);
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
