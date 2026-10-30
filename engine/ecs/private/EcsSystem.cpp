#include <EcsSystem.hpp>

#include <stdexcept>
#include <utility>

namespace aeg::ecs {

void Scheduler::add(System system) {
    if (!system) throw std::invalid_argument("Cannot add an empty ECS system");
    systems_.push_back(std::move(system));
}

void Scheduler::clear() noexcept { systems_.clear(); }

void Scheduler::run(World& world, const float deltaSeconds) const {
    const auto systems = systems_;
    for (const auto& system : systems) system(world, deltaSeconds);
}

} // namespace aeg::ecs
