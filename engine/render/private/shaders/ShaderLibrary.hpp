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
    ModelTaaFragment
};

[[nodiscard]] std::span<const std::byte> embeddedShader(ShaderId shader);
}
