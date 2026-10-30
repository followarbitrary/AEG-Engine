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
    auto& transform = state.transform();
    transform.rotation.y = 10;
    state.autoRotate() = true;
    state.systems.run(state.scene, 0.5f);
    require(std::abs(transform.rotation.y - 21.0f) < 0.001f, "ECS system did not update the selected transform");

    state.autoRotate() = false;
    state.systems.run(state.scene, 0.5f);
    require(std::abs(transform.rotation.y - 21.0f) < 0.001f, "disabled auto-rotation system changed the transform");
}
}

int main() {
    try {
        editorStateUsesEcsWorld();
        std::cout << "PASS editor ECS state\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL editor ECS state: " << error.what() << '\n';
        return 1;
    }
}
