#pragma once
#include <glm/glm.hpp>

namespace aeg::render {
struct RenderSettings {
    glm::mat4 model{1};
    glm::mat4 viewProjection{1};
    glm::vec3 lightDirection{-0.5f, -1.0f, -0.4f};
    float ambient{0.28f};
    glm::vec4 background{0.035f, 0.045f, 0.065f, 1};
    bool unlit{false};
    bool normalMapping{true};
    float normalStrength{1}; // Global multiplier on material normalScale, clamped to [0, 4].
};
}
