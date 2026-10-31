#include "SceneHierarchy.hpp"

#include <unordered_map>

namespace aeg::editor {
namespace {
enum class Visit : unsigned char { Unvisited, Visiting, Done };

struct Context {
    ecs::World& world;
    std::unordered_map<entt::entity, Visit> visits;
    std::unordered_map<entt::entity, math::Mat4> matrices;
    std::string error;

    explicit Context(ecs::World& value) : world(value) {}

    bool compute(const ecs::Entity entity) {
        if (!world.valid(entity)) {
            error = "hierarchy contains an invalid or foreign entity";
            return false;
        }
        const auto raw = entity.raw();
        const auto visit = visits[raw];
        if (visit == Visit::Done) return true;
        if (visit == Visit::Visiting) {
            error = "hierarchy cycle detected";
            return false;
        }
        visits[raw] = Visit::Visiting;
        auto& registry = world.registry();
        if (!registry.all_of<Transform>(entity)) {
            error = "hierarchy entity has no local Transform";
            return false;
        }
        const auto local = registry.get<Transform>(entity).matrix();
        if (!math::is_finite(local)) {
            error = "hierarchy contains a non-finite local transform";
            return false;
        }
        math::Mat4 matrix = local;
        if (registry.all_of<Parent>(entity)) {
            const auto parent = registry.get<Parent>(entity).entity;
            if (parent.raw() != entt::null) {
                if (!compute(parent)) return false;
                matrix = matrices.at(parent.raw()) * local;
            }
        }
        if (!math::is_finite(matrix)) {
            error = "hierarchy produced a non-finite world transform";
            return false;
        }
        matrices[raw] = matrix;
        visits[raw] = Visit::Done;
        return true;
    }
};
}

HierarchyUpdate updateWorldTransforms(ecs::World& world, const std::span<const ecs::Entity> entities) {
    Context context{world};
    for (const auto& entity : entities)
        if (!context.compute(entity)) return {false, std::move(context.error)};
    auto& registry = world.registry();
    for (const auto& entity : entities)
        registry.emplace_or_replace<WorldTransform>(entity, WorldTransform{context.matrices.at(entity.raw())});
    return {true, {}};
}

bool trySetParent(ecs::World& world, const ecs::Entity child, const ecs::Entity parent, std::string& error) {
    if (!world.valid(child) || (parent.raw() != entt::null && !world.valid(parent))) {
        error = "parent or child belongs to another World or is invalid";
        return false;
    }
    if (!world.registry().all_of<Transform>(child) ||
        (parent.raw() != entt::null && !world.registry().all_of<Transform>(parent))) {
        error = "parent and child must have local Transform components";
        return false;
    }
    for (auto current = parent; current.raw() != entt::null;) {
        if (current == child) {
            error = "hierarchy cycle detected";
            return false;
        }
        if (!world.valid(current)) {
            error = "parent chain contains an invalid entity";
            return false;
        }
        if (!world.registry().all_of<Parent>(current)) break;
        current = world.registry().get<Parent>(current).entity;
    }
    world.registry().emplace_or_replace<Parent>(child, Parent{parent});
    error.clear();
    return true;
}

} // namespace aeg::editor
