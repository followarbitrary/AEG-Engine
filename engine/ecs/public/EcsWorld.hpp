#pragma once

#include <entt/entity/registry.hpp>

#include <memory>
#include <utility>

namespace aeg::ecs {

namespace detail {
struct WorldIdentity {};
}

class World;

class Entity {
public:
    Entity() noexcept = default;

    // The raw value is suitable for queries through the owning World's registry.
    [[nodiscard]] entt::entity raw() const noexcept { return value_; }
    operator entt::entity() const noexcept { return value_; }

    friend bool operator==(const Entity& left, const Entity& right) noexcept {
        return left.value_ == right.value_ && left.identity_ == right.identity_;
    }
    friend bool operator!=(const Entity& left, const Entity& right) noexcept { return !(left == right); }
    friend constexpr bool operator==(const Entity& left, const entt::entity right) noexcept {
        return left.value_ == right;
    }
    friend constexpr bool operator==(const entt::entity left, const Entity& right) noexcept {
        return left == right.value_;
    }
    friend constexpr bool operator!=(const Entity& left, const entt::entity right) noexcept {
        return !(left == right);
    }
    friend constexpr bool operator!=(const entt::entity left, const Entity& right) noexcept {
        return !(left == right);
    }

private:
    Entity(const entt::entity value, std::shared_ptr<const detail::WorldIdentity> identity) noexcept
        : value_(value), identity_(std::move(identity)) {}

    entt::entity value_{entt::null};
    std::shared_ptr<const detail::WorldIdentity> identity_;
    friend class World;
};

class World {
public:
    World() : identity_(std::make_shared<detail::WorldIdentity>()) {}
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;

    [[nodiscard]] Entity create();
    [[nodiscard]] bool valid(Entity entity) const noexcept;
    // Returns false for a foreign or already-destroyed handle.
    [[nodiscard]] bool destroy(Entity entity) noexcept;
    void clear();

    [[nodiscard]] entt::registry& registry() noexcept { return registry_; }
    [[nodiscard]] const entt::registry& registry() const noexcept { return registry_; }

private:
    std::shared_ptr<const detail::WorldIdentity> identity_;
    entt::registry registry_;
};

} // namespace aeg::ecs
