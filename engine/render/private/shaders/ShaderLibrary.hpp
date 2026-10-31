#pragma once
#include <cstddef>
#include <span>

namespace aeg::render::detail {
enum class ShaderId {
    ModelVertex,
    ModelFragment,
    FullscreenVertex,
    FxaaFragment,
    TaaFragment,
    ModelTaaVertex,
    ModelTaaFragment,
    BindlessModelVertex,
    ShadowVertex,
    ShadowFragment,
    EnvironmentCompute,
    IrradianceCompute,
    PrefilterCompute,
    BrdfCompute,
    ToneMappingFragment,
    CullCompute,
    BindlessModelFragment
};

[[nodiscard]] std::span<const std::byte> embeddedShader(ShaderId shader);
}
