#pragma once

#include <entt/entity/registry.hpp>

namespace aeg::ecs {

class World {
public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;

    [[nodiscard]] entt::entity create();
    void destroy(entt::entity entity);
    void clear();

    [[nodiscard]] entt::registry& registry() noexcept { return registry_; }
    [[nodiscard]] const entt::registry& registry() const noexcept { return registry_; }

private:
    entt::registry registry_;
};

} // namespace aeg::ecs
