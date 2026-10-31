#pragma once
#include "gizmo/Transform.hpp"
#include "gizmo/ViewportGizmo.hpp"
#include "gizmo/OrientationGizmo.hpp"
#include "SceneHierarchy.hpp"
#include <Editor.hpp>
#include <AntialiasingSettings.hpp>
#include <Bounds.hpp>
#include <EcsSystem.hpp>
#include <ModelData.hpp>
#include <ModelResources.hpp>
#include <OrbitCamera.hpp>
#include <RenderSettings.hpp>
#include <RenderDebugSettings.hpp>
#include <Renderer.hpp>
#include <Utf8Path.hpp>

#include <math.hpp>
#include <imgui.h>
#include "EditorWidgets.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

struct GLFWwindow;

namespace aeg::editor {
inline constexpr const char* sceneWindow = "场景###Scene";
inline constexpr const char* importWindow = "导入###Import";
inline constexpr const char* inspectorWindow = "属性###Inspector";
inline constexpr const char* viewportWindow = "视口###Viewport";
inline constexpr const char* debugWindow = "调试###Debug";

struct WindowLifetime {
    bool initialized{false};
    GLFWwindow* window{nullptr};
    ImGuiContext* context{nullptr};
    ~WindowLifetime();
};

struct ImportQueue {
    std::deque<std::filesystem::path> paths;
    std::deque<assets::ModelHandle> pending;
    std::unordered_map<assets::ModelHandle, std::uint64_t> serials;
    std::uint64_t nextSerial{0};
    std::uint64_t latestSerial{0};
    std::string error;
};

struct AutoRotate {
    bool enabled{false};
    float degreesPerSecond{22.0f};
};

struct Renderable {
    assets::ModelData model;
};

struct CameraComponent {
    camera::OrbitCamera orbit;
};

struct LightComponent {
    render::Light value;
};

struct EditorState {
    ecs::World scene;
    ecs::Entity selectedEntity{};
    ecs::Entity cameraEntity{};
    std::vector<ecs::Entity> renderableEntities;
    std::vector<ecs::Entity> lightEntities;
    ecs::Scheduler systems;
    render::AntialiasingSettings antialiasing;
    render::RenderDebugSettings debugPasses;
    std::uint32_t supportedMsaa{1};
    bool supportedTaa{false};
    std::string antialiasingNotice;
    std::string toneMappingError;
    ViewportGizmoState gizmo;
    OrientationGizmoState orientationGizmo;
    render::RenderSettings settings;
    ImportQueue imports;
    std::deque<std::filesystem::path> recentFiles;
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
    std::array<char, 4096> environmentPath{};
    std::string environmentNotice;
    std::string environmentError;
    bool frameRequested{false};
    bool sceneRendered{false};
    std::array<bool, 3> viewportDrag{};
    std::uint32_t frames{0};
    std::uint32_t viewportWidth{1}, viewportHeight{1};
    std::string status{"就绪。将模型拖入窗口即可导入。"};
    std::string hierarchyError;
    bool debugPanelActive{false};
    std::size_t selectedMesh{0};
    bool materialDirty{false};
    bool materialNormalMapClear{false};

    EditorState() : selectedEntity(scene.create()), cameraEntity(scene.create()) {
        settings.frustumCulling = true;
        scene.registry().emplace<Transform>(selectedEntity);
        scene.registry().emplace<AutoRotate>(selectedEntity);
        scene.registry().emplace<Renderable>(selectedEntity);
        scene.registry().emplace<WorldTransform>(selectedEntity);
        renderableEntities.push_back(selectedEntity);
        scene.registry().emplace<CameraComponent>(cameraEntity);
        loadRecentFiles();
        const auto lightEntity = scene.create();
        scene.registry().emplace<LightComponent>(lightEntity);
        lightEntities.push_back(lightEntity);
        syncRenderSettingsFromEcs();
        const auto entity = selectedEntity;
        systems.add([entity](ecs::World& world, const float deltaSeconds) {
            auto& value = world.registry().get<Transform>(entity);
            const auto& rotate = world.registry().get<AutoRotate>(entity);
            if (!rotate.enabled) return;
            value.rotation.y = std::fmod(value.rotation.y + deltaSeconds * rotate.degreesPerSecond, 360.0f);
        });
    }

    [[nodiscard]] static std::filesystem::path recentFilesPath() {
#ifdef _WIN32
        if (const auto* appData = std::getenv("APPDATA"); appData && *appData)
            return std::filesystem::path(appData) / "AEG Engine" / "recent-models.txt";
#endif
        return std::filesystem::path(".aeg-recent-models.txt");
    }

    void loadRecentFiles() {
        std::ifstream input(recentFilesPath(), std::ios::in);
        std::string line;
        while (input && std::getline(input, line)) {
            if (line.empty()) continue;
            const auto path = foundation::pathFromUtf8(line);
            if (std::filesystem::is_regular_file(path) &&
                std::find(recentFiles.begin(), recentFiles.end(), path) == recentFiles.end())
                recentFiles.push_back(path);
            if (recentFiles.size() >= 5) break;
        }
    }

    void rememberRecentFile(const std::filesystem::path& path) {
        if (path.empty()) return;
        recentFiles.erase(std::remove(recentFiles.begin(), recentFiles.end(), path), recentFiles.end());
        recentFiles.push_front(path);
        while (recentFiles.size() > 5) recentFiles.pop_back();
        std::error_code error;
        std::filesystem::create_directories(recentFilesPath().parent_path(), error);
        std::ofstream output(recentFilesPath(), std::ios::trunc);
        if (!output) return;
        for (const auto& recent : recentFiles) output << foundation::pathToUtf8(recent) << '\n';
    }

    [[nodiscard]] Transform& transform() { return scene.registry().get<Transform>(selectedEntity); }
    [[nodiscard]] const Transform& transform() const { return scene.registry().get<Transform>(selectedEntity); }
    [[nodiscard]] camera::OrbitCamera& camera() { return scene.registry().get<CameraComponent>(cameraEntity).orbit; }
    [[nodiscard]] const camera::OrbitCamera& camera() const {
        return scene.registry().get<CameraComponent>(cameraEntity).orbit;
    }
    [[nodiscard]] bool& autoRotate() { return scene.registry().get<AutoRotate>(selectedEntity).enabled; }
    [[nodiscard]] const bool& autoRotate() const { return scene.registry().get<AutoRotate>(selectedEntity).enabled; }
    [[nodiscard]] assets::ModelData& model() { return scene.registry().get<Renderable>(selectedEntity).model; }
    [[nodiscard]] const assets::ModelData& model() const {
        return scene.registry().get<Renderable>(selectedEntity).model;
    }
    [[nodiscard]] const math::Mat4& worldMatrix() const {
        return scene.registry().get<WorldTransform>(selectedEntity).matrix;
    }
    [[nodiscard]] bool updateWorldTransforms() {
        const auto result = aeg::editor::updateWorldTransforms(scene, renderableEntities);
        hierarchyError = result.error;
        return result.success;
    }
    [[nodiscard]] bool setParent(const ecs::Entity child, const ecs::Entity parent) {
        const bool hadParent = scene.registry().all_of<Parent>(child);
        const auto previous = hadParent ? scene.registry().get<Parent>(child).entity : ecs::Entity{};
        std::string error;
        if (!trySetParent(scene, child, parent, error)) {
            hierarchyError = std::move(error);
            return false;
        }
        if (updateWorldTransforms()) return true;
        if (hadParent)
            scene.registry().emplace_or_replace<Parent>(child, Parent{previous});
        else
            scene.registry().remove<Parent>(child);
        (void)updateWorldTransforms();
        return false;
    }
    [[nodiscard]] std::size_t lightCount() const noexcept { return lightEntities.size(); }
    [[nodiscard]] render::Light& light(const std::size_t index) {
        return scene.registry().get<LightComponent>(lightEntities.at(index)).value;
    }
    [[nodiscard]] const render::Light& light(const std::size_t index) const {
        return scene.registry().get<LightComponent>(lightEntities.at(index)).value;
    }
    void syncRenderSettingsFromEcs() {
        settings.lightCount = static_cast<std::uint32_t>(std::min<std::size_t>(lightEntities.size(), render::kMaxLights));
        for (std::uint32_t index = 0; index < settings.lightCount; ++index) settings.lights[index] = light(index);
    }
    [[nodiscard]] ecs::Entity createRenderable(std::string name = {}) {
        const auto entity = scene.create();
        scene.registry().emplace<Transform>(entity);
        scene.registry().emplace<WorldTransform>(entity);
        auto& renderable = scene.registry().emplace<Renderable>(entity);
        renderable.model.name = std::move(name);
        renderableEntities.push_back(entity);
        return entity;
    }
    void destroyRenderable(const ecs::Entity entity) {
        if (entity == selectedEntity) return;
        const auto found = std::find(renderableEntities.begin(), renderableEntities.end(), entity);
        if (found == renderableEntities.end()) return;
        for (const auto& other : renderableEntities) {
            if (other == entity || !scene.registry().all_of<Parent>(other)) continue;
            if (scene.registry().get<Parent>(other).entity == entity) scene.registry().remove<Parent>(other);
        }
        static_cast<void>(scene.destroy(entity));
        renderableEntities.erase(found);
    }
};

void initializeEditorWindow(WindowLifetime& lifetime, const EditorOptions& options);
void bindImportQueue(GLFWwindow* window, ImportQueue& imports);
[[nodiscard]] std::filesystem::path browseModelFile(GLFWwindow* window);
void configureStyle();
[[nodiscard]] ImGuiWindowFlags workspacePanelFlags();
void drawWorkspace(EditorState& state, render::Renderer& renderer);
void processImports(EditorState& state, render::Renderer& renderer, assets::ModelResources& resources);
[[nodiscard]] bool addLight(EditorState& state);
void drawScenePanel(EditorState& state);
void drawImportPanel(EditorState& state);
void resetMaterialEditor(EditorState& state);
void drawMaterialSection(EditorState& state, render::Renderer& renderer);
void drawLightingSection(EditorState& state, render::Renderer& renderer);
void drawShadowsSection(EditorState& state, render::Renderer& renderer);
void drawEnvironmentSection(EditorState& state, render::Renderer& renderer);
void drawToneMappingSection(EditorState& state, render::Renderer& renderer);
void drawInspectorPanel(EditorState& state, render::Renderer& renderer, EditorGraphics& graphics);
void drawViewportPanel(EditorState& state, render::Renderer& renderer, EditorGraphics& graphics);
void drawDebugContents(EditorState& state, render::Renderer& renderer, EditorGraphics& graphics);
}
