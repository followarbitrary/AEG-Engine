#pragma once

#include <EcsWorld.hpp>

#include <cstddef>
#include <functional>
#include <vector>

namespace aeg::ecs {

using System = std::function<void(World&, float)>;

class Scheduler {
public:
    Scheduler() = default;
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) noexcept = default;
    Scheduler& operator=(Scheduler&&) noexcept = default;

    void add(System system);
    void clear() noexcept;
    void run(World& world, float deltaSeconds) const;

    [[nodiscard]] std::size_t size() const noexcept { return systems_.size(); }

private:
    std::vector<System> systems_;
};

} // namespace aeg::ecs
