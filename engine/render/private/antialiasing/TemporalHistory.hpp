#pragma once
#include <RenderSettings.hpp>
#include <math.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace aeg::render {
struct TemporalPush {
    aeg::math::Vec2 inverseResolution;
    float historyWeight{};
    float depthThreshold{};
    std::uint32_t historyValid{};
};
static_assert(std::is_standard_layout_v<TemporalPush> && std::is_trivially_copyable_v<TemporalPush>);
static_assert(sizeof(TemporalPush) == 20 && offsetof(TemporalPush, inverseResolution) == 0);
static_assert(offsetof(TemporalPush, historyWeight) == 8 && offsetof(TemporalPush, depthThreshold) == 12 &&
              offsetof(TemporalPush, historyValid) == 16);
static_assert(sizeof(math::Vec2) == 8 && std::is_trivially_copyable_v<math::Vec2>); // FXAA push ABI

struct TemporalHistory {
    bool valid{};
    std::uint32_t frames{}, readIndex{1}, jitterIndex{};
    RenderSettings previous;
};
}
