#pragma once
#include "gizmo/Transform.hpp"
#include "gizmo/ViewportGizmo.hpp"
#include "gizmo/OrientationGizmo.hpp"
#include <Editor.hpp>
#include <AntialiasingSettings.hpp>
#include <Bounds.hpp>
#include <ModelData.hpp>
#include <OrbitCamera.hpp>
#include <RenderSettings.hpp>
#include <Renderer.hpp>

#include <math.hpp>
#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>

struct GLFWwindow;

namespace aeg::editor {
inline constexpr const char* sceneWindow = "场景###Scene";
inline constexpr const char* importWindow = "导入###Import";
inline constexpr const char* inspectorWindow = "属性###Inspector";
inline constexpr const char* viewportWindow = "视口###Viewport";

struct WindowLifetime {
    bool initialized{false};
    GLFWwindow* window{nullptr};
    ImGuiContext* context{nullptr};
    ~WindowLifetime();
};

struct ImportQueue {
    std::deque<std::filesystem::path> paths;
    std::string error;
};

struct EditorState {
    render::AntialiasingSettings antialiasing;
    std::uint32_t supportedMsaa{1};
    bool supportedTaa{false};
    std::string antialiasingNotice;
    assets::ModelData model;
    camera::OrbitCamera camera;
    Transform transform;
    ViewportGizmoState gizmo;
    OrientationGizmoState orientationGizmo;
    render::RenderSettings settings;
    ImportQueue imports;
    std::array<char, 4096> importPath{};
    std::size_t selectedMaterial{0};
    bool materialEditorInitialized{false};
    std::array<char, 4096> normalMapPath{};
    assets::AlphaMode materialAlphaMode{assets::AlphaMode::Opaque};
    float materialOpacity{1};
    float materialAlphaCutoff{0.5f};
    float materialMetallic{0};
    float materialRoughness{0.5f};
    std::string materialNotice;
    std::string materialError;
    std::uint32_t selectedLight{0};
    int newLightType{1};
    std::string shadowError;
    bool autoRotate{false};
    bool frameRequested{false};
    bool sceneRendered{false};
    std::array<bool, 3> viewportDrag{};
    std::uint32_t frames{0};
    std::uint32_t viewportWidth{1}, viewportHeight{1};
    std::string status{"就绪。将模型拖入窗口即可导入。"};
};

void initializeEditorWindow(WindowLifetime& lifetime, const EditorOptions& options);
void bindImportQueue(GLFWwindow* window, ImportQueue& imports);
void configureStyle();
void drawWorkspace(render::Renderer& renderer);
void processImports(EditorState& state, render::Renderer& renderer);
void drawScenePanel(EditorState& state);
void drawImportPanel(EditorState& state);
void resetMaterialEditor(EditorState& state);
void drawMaterialSection(EditorState& state, render::Renderer& renderer);
void drawLightingSection(EditorState& state, render::Renderer& renderer);
void drawInspectorPanel(EditorState& state, render::Renderer& renderer);
void drawViewportPanel(EditorState& state, render::Renderer& renderer, EditorGraphics& graphics);
}
