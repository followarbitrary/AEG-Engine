#pragma once
#include <RenderSettings.hpp>
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>

namespace aeg::render {
struct TemporalPush {
    glm::vec2 inverseResolution;
    float historyWeight{};
    std::uint32_t historyValid{};
};
static_assert(sizeof(TemporalPush) == 16 && offsetof(TemporalPush, historyValid) == 12);

struct TemporalHistory {
    bool valid{};
    std::uint32_t frames{}, readIndex{1}, jitterIndex{};
    RenderSettings previous;
};
}
