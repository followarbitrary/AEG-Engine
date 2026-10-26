#pragma once
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>

namespace aeg::render {
struct alignas(16) SceneUniform {
    glm::mat4 viewProjection{1};
    glm::mat4 model{1};
    glm::vec4 lightDirection{0, -1, 0, 0};
    glm::vec4 parameters{0.28f, 0, 0, 0};
    glm::mat4 previousViewProjection{1};
    glm::mat4 previousModel{1};
    glm::mat4 currentViewProjection{1};
    glm::vec4 temporalInfo{};
};
static_assert(sizeof(SceneUniform) == 368);

struct MaterialPush {
    glm::vec4 color{1};
    float alphaCutoff{0.5f};
    std::uint32_t alphaMasked{};
    float normalScale{1};
    std::uint32_t hasNormalMap{};
};
static_assert(sizeof(MaterialPush) == 32);
static_assert(offsetof(MaterialPush, alphaCutoff) == 16 && offsetof(MaterialPush, alphaMasked) == 20);
static_assert(offsetof(MaterialPush, normalScale) == 24 && offsetof(MaterialPush, hasNormalMap) == 28);
}
