#pragma once

#include "gizmo/Transform.hpp"

#include <EcsWorld.hpp>
#include <math.hpp>

#include <span>
#include <string>
#include <vector>

namespace aeg::editor {

struct Parent {
    ecs::Entity entity{};
};

struct WorldTransform {
    math::Mat4 matrix{math::identity()};
};

struct HierarchyUpdate {
    bool success{false};
    std::string error;
};

[[nodiscard]] HierarchyUpdate updateWorldTransforms(ecs::World& world, std::span<const ecs::Entity> entities);
[[nodiscard]] bool trySetParent(ecs::World& world, ecs::Entity child, ecs::Entity parent, std::string& error);

} // namespace aeg::editor
