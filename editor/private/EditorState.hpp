#pragma once
#include "gizmo/Transform.hpp"
#include "gizmo/ViewportGizmo.hpp"
#include "gizmo/OrientationGizmo.hpp"
#include <Editor.hpp>
#include <AntialiasingSettings.hpp>
#include <Bounds.hpp>
#include <EcsSystem.hpp>
#include <ModelData.hpp>
#include <OrbitCamera.hpp>
#include <RenderSettings.hpp>
#include <Renderer.hpp>

#include <math.hpp>
#include <imgui.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

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
    entt::entity selectedEntity{entt::null};
    entt::entity cameraEntity{entt::null};
    std::vector<entt::entity> renderableEntities;
    std::vector<entt::entity> lightEntities;
    ecs::Scheduler systems;
    render::AntialiasingSettings antialiasing;
    std::uint32_t supportedMsaa{1};
    bool supportedTaa{false};
    std::string antialiasingNotice;
    std::string toneMappingError;
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
    std::array<char, 4096> environmentPath{};
    std::string environmentNotice;
    std::string environmentError;
    bool frameRequested{false};
    bool sceneRendered{false};
    std::array<bool, 3> viewportDrag{};
    std::uint32_t frames{0};
    std::uint32_t viewportWidth{1}, viewportHeight{1};
    std::string status{"就绪。将模型拖入窗口即可导入。"};

    EditorState() : selectedEntity(scene.create()), cameraEntity(scene.create()) {
        scene.registry().emplace<Transform>(selectedEntity);
        scene.registry().emplace<AutoRotate>(selectedEntity);
        scene.registry().emplace<Renderable>(selectedEntity);
        renderableEntities.push_back(selectedEntity);
        scene.registry().emplace<CameraComponent>(cameraEntity);
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
    [[nodiscard]] entt::entity createRenderable(std::string name = {}) {
        const auto entity = scene.create();
        scene.registry().emplace<Transform>(entity);
        auto& renderable = scene.registry().emplace<Renderable>(entity);
        renderable.model.name = std::move(name);
        renderableEntities.push_back(entity);
        return entity;
    }
    void destroyRenderable(const entt::entity entity) {
        if (entity == selectedEntity) return;
        const auto found = std::find(renderableEntities.begin(), renderableEntities.end(), entity);
        if (found == renderableEntities.end()) return;
        scene.destroy(entity);
        renderableEntities.erase(found);
    }
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
void drawEnvironmentSection(EditorState& state, render::Renderer& renderer);
void drawToneMappingSection(EditorState& state, render::Renderer& renderer);
void drawInspectorPanel(EditorState& state, render::Renderer& renderer);
void drawViewportPanel(EditorState& state, render::Renderer& renderer, EditorGraphics& graphics);
}
