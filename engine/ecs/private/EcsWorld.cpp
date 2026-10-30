#include <EcsWorld.hpp>

namespace aeg::ecs {

entt::entity World::create() { return registry_.create(); }

void World::destroy(const entt::entity entity) {
    if (registry_.valid(entity)) registry_.destroy(entity);
}

void World::clear() { registry_.clear(); }

} // namespace aeg::ecs
