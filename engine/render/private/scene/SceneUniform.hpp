#pragma once
#include <math.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace aeg::render {
struct alignas(16) SceneUniform {
    aeg::math::Mat4 viewProjection{aeg::math::identity()};
    aeg::math::Mat4 model{aeg::math::identity()};
    aeg::math::Vec4 lightDirection{0, -1, 0, 0};
    aeg::math::Vec4 parameters{0.28f, 0, 0, 0};
    math::Vec4 clipConvention{1, 0, 1, 0};
    math::Vec4 cameraPosition{};
    math::Vec4 cameraTowardViewer{0, 0, 1, 0};
    math::Vec4 lightRadiance{3, 3, 3, 0};
    aeg::math::Mat4 previousViewProjection{aeg::math::identity()};
    aeg::math::Mat4 previousModel{aeg::math::identity()};
    aeg::math::Mat4 currentViewProjection{aeg::math::identity()};
    aeg::math::Vec4 temporalInfo{};
};
static_assert(std::is_standard_layout_v<SceneUniform> && std::is_trivially_copyable_v<SceneUniform>);
static_assert(alignof(SceneUniform) == 16 && sizeof(SceneUniform) == 432);
static_assert(offsetof(SceneUniform, viewProjection) == 0 && offsetof(SceneUniform, model) == 64);
static_assert(offsetof(SceneUniform, lightDirection) == 128 && offsetof(SceneUniform, parameters) == 144);
static_assert(offsetof(SceneUniform, clipConvention) == 160);
static_assert(offsetof(SceneUniform, cameraPosition) == 176 && offsetof(SceneUniform, cameraTowardViewer) == 192);
static_assert(offsetof(SceneUniform, lightRadiance) == 208);
static_assert(offsetof(SceneUniform, previousViewProjection) == 224 && offsetof(SceneUniform, previousModel) == 288);
static_assert(offsetof(SceneUniform, currentViewProjection) == 352 && offsetof(SceneUniform, temporalInfo) == 416);

struct MaterialPush {
    aeg::math::Vec4 color{1};
    float alphaCutoff{0.5f};
    std::uint32_t alphaMode{};
    float normalScale{1};
    std::uint32_t hasNormalMap{};
    math::Vec4 pbrFactors{0, 0.5f, 0, 0};
};
static_assert(std::is_standard_layout_v<MaterialPush> && std::is_trivially_copyable_v<MaterialPush>);
static_assert(alignof(MaterialPush) == 16 && sizeof(MaterialPush) == 48 && offsetof(MaterialPush, color) == 0);
static_assert(offsetof(MaterialPush, alphaCutoff) == 16 && offsetof(MaterialPush, alphaMode) == 20);
static_assert(offsetof(MaterialPush, normalScale) == 24 && offsetof(MaterialPush, hasNormalMap) == 28);
static_assert(offsetof(MaterialPush, pbrFactors) == 32);
}
