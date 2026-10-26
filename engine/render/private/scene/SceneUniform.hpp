#pragma once
#include <math.hpp>
#include <Light.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace aeg::render {
struct alignas(16) GpuLight {
    math::Vec4 positionRange{};
    math::Vec4 directionType{0, -1, 0, 0};
    math::Vec4 radiance{};       // RGB radiance, enabled flag in w.
    math::Vec4 cone{1, 0, 0, 0}; // Cosine of the inner and outer half angles.
};
static_assert(std::is_standard_layout_v<GpuLight> && std::is_trivially_copyable_v<GpuLight>);
static_assert(sizeof(GpuLight) == 64 && alignof(GpuLight) == 16);
static_assert(offsetof(GpuLight, positionRange) == 0 && offsetof(GpuLight, directionType) == 16);
static_assert(offsetof(GpuLight, radiance) == 32 && offsetof(GpuLight, cone) == 48);

struct alignas(16) SceneUniform {
    aeg::math::Mat4 viewProjection{aeg::math::identity()};
    aeg::math::Mat4 model{aeg::math::identity()};
    aeg::math::Vec4 parameters{0.28f, 0, 0, 0};
    math::Vec4 clipConvention{1, 0, 1, 0};
    math::Vec4 cameraPosition{};
    math::Vec4 cameraTowardViewer{0, 0, 1, 0};
    aeg::math::Mat4 previousViewProjection{aeg::math::identity()};
    aeg::math::Mat4 previousModel{aeg::math::identity()};
    aeg::math::Mat4 currentViewProjection{aeg::math::identity()};
    aeg::math::Vec4 temporalInfo{};
    math::Vec4 lightInfo{};
    std::array<GpuLight, kMaxLights> lights{};
    math::Mat4 lightViewProjection{math::identity()};
    math::Vec4 shadowInfo{}; // Enabled, selected light index, inverse square-map resolution, reserved.
    math::Vec4 shadowBias{}; // Constant and slope depth bias.
};
static_assert(std::is_standard_layout_v<SceneUniform> && std::is_trivially_copyable_v<SceneUniform>);
static_assert(alignof(SceneUniform) == 16 && sizeof(SceneUniform) == 1024);
static_assert(offsetof(SceneUniform, viewProjection) == 0 && offsetof(SceneUniform, model) == 64);
static_assert(offsetof(SceneUniform, parameters) == 128 && offsetof(SceneUniform, clipConvention) == 144);
static_assert(offsetof(SceneUniform, cameraPosition) == 160 && offsetof(SceneUniform, cameraTowardViewer) == 176);
static_assert(offsetof(SceneUniform, previousViewProjection) == 192 && offsetof(SceneUniform, previousModel) == 256);
static_assert(offsetof(SceneUniform, currentViewProjection) == 320 && offsetof(SceneUniform, temporalInfo) == 384);
static_assert(offsetof(SceneUniform, lightInfo) == 400 && offsetof(SceneUniform, lights) == 416);
static_assert(offsetof(SceneUniform, lightViewProjection) == 928 && offsetof(SceneUniform, shadowInfo) == 992);
static_assert(offsetof(SceneUniform, shadowBias) == 1008);

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
