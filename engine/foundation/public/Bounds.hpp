#pragma once
#include <math.hpp>
#include <algorithm>

namespace aeg::foundation {
struct Bounds {
    math::Vec3 min{0};
    math::Vec3 max{0};
    [[nodiscard]] math::Vec3 center() const { return (min + max) * 0.5f; }
    [[nodiscard]] float radius() const { return std::max(math::length(max - min) * 0.5f, 0.01f); }
};
}
