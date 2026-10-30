#include <EcsWorld.hpp>

#include <iostream>
#include <stdexcept>

namespace {
struct Position {
    int x{};
    int y{};
};

struct Velocity {
    int x{};
    int y{};
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void worldLifecycle() {
    aeg::ecs::World world;
    const auto moving = world.create();
    const auto stationary = world.create();
    auto& registry = world.registry();

    registry.emplace<Position>(moving, 1, 2);
    registry.emplace<Velocity>(moving, 3, 4);
    registry.emplace<Position>(stationary, 5, 6);
    registry.patch<Position>(moving, [](Position& position) { position.x += 10; });

    const auto& position = registry.get<Position>(moving);
    require(position.x == 11 && position.y == 2, "component patch did not update the entity");
    require(registry.all_of<Velocity>(moving), "velocity component was not attached");
    registry.remove<Velocity>(moving);
    require(!registry.any_of<Velocity>(moving), "component removal did not detach velocity");

    std::size_t movingCount{};
    for (const auto entity : registry.view<Position, Velocity>()) {
        static_cast<void>(entity);
        ++movingCount;
    }
    require(movingCount == 0, "view returned an entity without all requested components");

    registry.emplace<Velocity>(moving, 7, 8);
    for (const auto entity : registry.view<Position, Velocity>()) {
        require(entity == moving, "view returned the wrong entity");
    }

    world.destroy(stationary);
    require(!registry.valid(stationary), "destroyed entity remained valid");
    require(registry.valid(moving), "destroying one entity invalidated another");

    world.clear();
    require(registry.storage<Position>().empty(), "clear did not release position components");
    require(registry.storage<Velocity>().empty(), "clear did not release velocity components");
    require(!registry.valid(moving), "clear did not invalidate entities");
}
}

int main() {
    try {
        worldLifecycle();
        std::cout << "PASS EnTT ECS world lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL EnTT ECS world lifecycle: " << error.what() << '\n';
        return 1;
    }
}
