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

    require(world.destroy(stationary), "destroyed entity was not accepted by its world");
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
    require(world.destroy(original), "initial entity destroy failed");
    require(!world.destroy(original), "double destroy unexpectedly succeeded");
    const auto recycled = world.create();
    require(recycled != original, "recycled entity did not receive a new generation");
    require(!world.destroy(original), "stale entity destroyed a recycled entity");
    require(world.registry().valid(recycled), "stale entity invalidated a recycled entity");

    aeg::ecs::World moved = std::move(world);
    const aeg::ecs::World& constWorld = moved;
    require(constWorld.registry().valid(recycled), "move construction lost the registry contents");

    aeg::ecs::World assigned;
    assigned = std::move(moved);
    const aeg::ecs::World& constAssigned = assigned;
    require(constAssigned.registry().valid(recycled), "move assignment lost the registry contents");
    require(assigned.destroy(recycled), "move assignment lost entity ownership");
}

void worldRejectsForeignEntities() {
    aeg::ecs::World first;
    aeg::ecs::World second;
    const auto firstEntity = first.create();
    const auto secondEntity = second.create();
    require(firstEntity != secondEntity, "entities from different worlds compared equal");
    first.registry().emplace<Position>(firstEntity, 7, 8);
    second.registry().emplace<Position>(secondEntity, 9, 10);

    require(!first.destroy(secondEntity), "destroy accepted an entity owned by another world");
    require(first.registry().valid(firstEntity), "foreign destroy invalidated the local entity");
    require(second.registry().valid(secondEntity), "foreign destroy changed the source world");
    require(first.destroy(firstEntity), "destroy rejected an entity owned by the world");
    require(!first.destroy(firstEntity), "destroy reported success for a stale entity");
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

void schedulerDefersMutationUntilNextRun() {
    aeg::ecs::World world;
    aeg::ecs::Scheduler scheduler;
    std::vector<int> order;
    scheduler.add([&](aeg::ecs::World&, const float) {
        order.push_back(1);
        scheduler.add([&](aeg::ecs::World&, const float) { order.push_back(3); });
    });
    scheduler.add([&](aeg::ecs::World&, const float) { order.push_back(2); });

    scheduler.run(world, 0);
    require(order == std::vector<int>{1, 2}, "systems added during a run executed in the same frame");
    require(scheduler.size() == 3, "system added during a run was not retained");
    scheduler.run(world, 0);
    require(order == std::vector<int>{1, 2, 1, 2, 3},
            "systems added during a run did not execute on the following frame");
}

void schedulerCanClearDuringRun() {
    aeg::ecs::World world;
    aeg::ecs::Scheduler scheduler;
    std::vector<int> order;
    scheduler.add([&](aeg::ecs::World&, const float) {
        order.push_back(1);
        scheduler.clear();
    });
    scheduler.add([&](aeg::ecs::World&, const float) { order.push_back(2); });

    scheduler.run(world, 0);
    require(order == std::vector<int>{1, 2}, "clearing during a run changed the current execution set");
    require(scheduler.size() == 0, "clear during a run did not take effect after the current snapshot");
}
}

int main() {
    try {
        worldLifecycle();
        worldBoundaryContracts();
        worldRejectsForeignEntities();
        systemSchedulerContracts();
        schedulerDefersMutationUntilNextRun();
        schedulerCanClearDuringRun();
        std::cout << "PASS EnTT ECS world lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL EnTT ECS world lifecycle: " << error.what() << '\n';
        return 1;
    }
}
