#include <EcsWorld.hpp>

namespace aeg::ecs {

Entity World::create() {
    if (!identity_) identity_ = std::make_shared<detail::WorldIdentity>();
    return Entity{registry_.create(), identity_};
}

bool World::valid(const Entity entity) const noexcept {
    return identity_ && entity.identity_ == identity_ && registry_.valid(entity.value_);
}

bool World::destroy(const Entity entity) noexcept {
    if (!valid(entity)) return false;
    registry_.destroy(entity.value_);
    return true;
}

void World::clear() { registry_.clear(); }

} // namespace aeg::ecs
