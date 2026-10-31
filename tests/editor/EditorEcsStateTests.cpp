#include "EditorState.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void editorStateUsesEcsWorld() {
    aeg::editor::EditorState state;
    require(state.scene.registry().valid(state.selectedEntity), "editor state did not create its selected entity");
    require(state.scene.registry().all_of<aeg::editor::Renderable>(state.selectedEntity),
            "editor state did not attach a renderable model component");
    state.model().name = "ecs-model";
    require(state.scene.registry().get<aeg::editor::Renderable>(state.selectedEntity).model.name == "ecs-model",
            "model accessor is not backed by the ECS renderable component");
    require(state.scene.registry().valid(state.cameraEntity) &&
                state.scene.registry().all_of<aeg::editor::CameraComponent>(state.cameraEntity),
            "editor state did not attach an ECS camera component");
    require(state.lightCount() == 1 &&
                state.scene.registry().all_of<aeg::editor::LightComponent>(state.lightEntities[0]),
            "editor state did not create its default ECS light");
    state.light(0).intensity = 3.0f;
    state.syncRenderSettingsFromEcs();
    require(state.settings.lightCount == 1 && state.settings.lights[0].intensity == 3.0f,
            "ECS light was not copied into render settings");

    const auto second = state.createRenderable("second");
    require(state.renderableEntities.size() == 2 && state.scene.registry().valid(second),
            "scene did not create a second renderable entity");
    require(state.scene.registry().view<aeg::editor::Transform, aeg::editor::Renderable>().size_hint() == 2,
            "scene view did not contain both renderable entities");
    state.destroyRenderable(second);
    require(state.renderableEntities.size() == 1 && !state.scene.registry().valid(second),
            "scene did not remove the second renderable entity");
    auto& transform = state.transform();
    transform.rotation.y = 10;
    state.autoRotate() = true;
    state.systems.run(state.scene, 0.5f);
    require(std::abs(transform.rotation.y - 21.0f) < 0.001f, "ECS system did not update the selected transform");

    state.autoRotate() = false;
    state.systems.run(state.scene, 0.5f);
    require(std::abs(transform.rotation.y - 21.0f) < 0.001f, "disabled auto-rotation system changed the transform");
}

void addingLightSelectsTheNewEntity() {
    aeg::editor::EditorState state;
    const auto previousCount = state.lightCount();
    require(aeg::editor::addLight(state), "adding a light should succeed below the maximum");
    require(state.lightCount() == previousCount + 1, "adding a light did not create an ECS entity");
    require(state.selectedLight == previousCount, "adding a light did not select the new light");
}

void hierarchyPropagatesAndRejectsCycles() {
    aeg::editor::EditorState state;
    const auto parent = state.createRenderable("parent");
    const auto child = state.createRenderable("child");
    state.scene.registry().get<aeg::editor::Transform>(parent).translation = {2, 0, 0};
    state.scene.registry().get<aeg::editor::Transform>(child).translation = {0, 3, 0};
    require(state.setParent(child, parent), "valid parent hierarchy was rejected");

    require(state.updateWorldTransforms(), "valid parent hierarchy failed to update");
    const auto childWorld = state.scene.registry().get<aeg::editor::WorldTransform>(child).matrix;
    require(aeg::math::nearly_equal(aeg::math::transform_point(childWorld, {0, 0, 0}), {2, 3, 0}, 0.0001f),
            "child world transform did not include its parent");

    require(!state.setParent(parent, child), "cyclic hierarchy was accepted");
    require(state.hierarchyError.find("cycle") != std::string::npos,
            "cyclic hierarchy did not produce a diagnostic");

    aeg::ecs::World foreignWorld;
    const auto foreign = foreignWorld.create();
    require(!state.setParent(child, foreign), "foreign parent entity was accepted");
}
}

int main() {
    try {
        editorStateUsesEcsWorld();
        addingLightSelectsTheNewEntity();
        hierarchyPropagatesAndRejectsCycles();
        std::cout << "PASS editor ECS state\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL editor ECS state: " << error.what() << '\n';
        return 1;
    }
}
