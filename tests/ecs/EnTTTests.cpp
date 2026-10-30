#include <EcsWorld.hpp>
#include <EcsSystem.hpp>

#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

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

void worldBoundaryContracts() {
    aeg::ecs::World world;
    const auto original = world.create();
    world.destroy(original);
    world.destroy(original);
    const auto recycled = world.create();
    require(recycled != original, "recycled entity did not receive a new generation");
    world.destroy(original);
    require(world.registry().valid(recycled), "stale entity destroyed a recycled entity");

    aeg::ecs::World moved = std::move(world);
    const aeg::ecs::World& constWorld = moved;
    require(constWorld.registry().valid(recycled), "move construction lost the registry contents");

    aeg::ecs::World assigned;
    assigned = std::move(moved);
    const aeg::ecs::World& constAssigned = assigned;
    require(constAssigned.registry().valid(recycled), "move assignment lost the registry contents");
}

void systemSchedulerContracts() {
    aeg::ecs::World world;
    struct Counter {
        int value{};
    };
    const auto entity = world.create();
    world.registry().emplace<Counter>(entity, 1);
    aeg::ecs::Scheduler scheduler;
    std::vector<int> order;
    float observedDelta{};
    scheduler.add([&](aeg::ecs::World& currentWorld, const float deltaSeconds) {
        order.push_back(1);
        observedDelta = deltaSeconds;
        ++currentWorld.registry().get<Counter>(entity).value;
    });
    scheduler.add([&](aeg::ecs::World& currentWorld, const float deltaSeconds) {
        order.push_back(2);
        require(deltaSeconds == observedDelta, "systems did not receive the same delta time");
        currentWorld.registry().get<Counter>(entity).value *= 2;
    });
    require(scheduler.size() == 2, "system registration count is incorrect");
    scheduler.run(world, 1.0f / 60.0f);
    require(order == std::vector<int>{1, 2}, "systems did not run in registration order");
    require(world.registry().get<Counter>(entity).value == 4, "systems did not update ECS components");

    bool rejectedEmpty{};
    try {
        scheduler.add({});
    } catch (const std::invalid_argument&) {
        rejectedEmpty = true;
    }
    require(rejectedEmpty, "empty systems were accepted");

    scheduler.clear();
    require(scheduler.size() == 0, "system clear did not remove registered systems");
}
}

int main() {
    try {
        worldLifecycle();
        worldBoundaryContracts();
        systemSchedulerContracts();
        std::cout << "PASS EnTT ECS world lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL EnTT ECS world lifecycle: " << error.what() << '\n';
        return 1;
    }
}
