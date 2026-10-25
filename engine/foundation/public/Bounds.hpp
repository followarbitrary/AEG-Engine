#pragma once
#include <glm/glm.hpp>

namespace aeg::foundation {
struct Bounds {
    glm::vec3 min{0};
    glm::vec3 max{0};
    [[nodiscard]] glm::vec3 center() const { return (min + max) * 0.5f; }
    [[nodiscard]] float radius() const { return glm::max(glm::length(max - min) * 0.5f, 0.01f); }
};
}
