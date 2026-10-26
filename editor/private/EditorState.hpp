#pragma once
#include <Editor.hpp>
#include <AntialiasingSettings.hpp>
#include <Bounds.hpp>
#include <ModelData.hpp>
#include <OrbitCamera.hpp>
#include <RenderSettings.hpp>
#include <Renderer.hpp>

#include <glm/gtc/matrix_transform.hpp>
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

struct Transform {
    glm::vec3 translation{0};
    glm::vec3 rotation{0};
    glm::vec3 scale{1};
    glm::mat4 matrix() const {
        auto result = glm::translate(glm::mat4(1), translation);
        result = glm::rotate(result, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        result = glm::rotate(result, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        result = glm::rotate(result, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        return glm::scale(result, scale);
    }
};

struct EditorState {
    render::AntialiasingSettings antialiasing;
    std::uint32_t supportedMsaa{1};
    bool supportedTaa{false};
    std::string antialiasingNotice;
    assets::ModelData model;
    camera::OrbitCamera camera;
    Transform transform;
    render::RenderSettings settings;
    ImportQueue imports;
    std::array<char, 4096> importPath{};
    std::size_t selectedMaterial{0};
    bool materialEditorInitialized{false};
    std::array<char, 4096> normalMapPath{};
    std::string materialNotice;
    std::string materialError;
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
void drawInspectorPanel(EditorState& state, render::Renderer& renderer);
void drawViewportPanel(EditorState& state, render::Renderer& renderer, EditorGraphics& graphics);
}
