#include "ShaderLibrary.hpp"
#include <embedded_shaders/model.vert.spv.hpp>
#include <embedded_shaders/model.frag.spv.hpp>
#include <embedded_shaders/fullscreen.vert.spv.hpp>
#include <embedded_shaders/fxaa.frag.spv.hpp>
#include <embedded_shaders/taa.frag.spv.hpp>
#include <embedded_shaders/model_taa.vert.spv.hpp>
#include <embedded_shaders/model_taa.frag.spv.hpp>
#include <embedded_shaders/shadow.vert.spv.hpp>
#include <embedded_shaders/shadow.frag.spv.hpp>
#include <stdexcept>

namespace aeg::render::detail {
std::span<const std::byte> embeddedShader(ShaderId shader) {
    switch (shader) {
    case ShaderId::ModelVertex:
        return std::as_bytes(std::span{embedded_shaders::model_vert});
    case ShaderId::ModelFragment:
        return std::as_bytes(std::span{embedded_shaders::model_frag});
    case ShaderId::FullscreenVertex:
        return std::as_bytes(std::span{embedded_shaders::fullscreen_vert});
    case ShaderId::FxaaFragment:
        return std::as_bytes(std::span{embedded_shaders::fxaa_frag});
    case ShaderId::TaaFragment:
        return std::as_bytes(std::span{embedded_shaders::taa_frag});
    case ShaderId::ModelTaaVertex:
        return std::as_bytes(std::span{embedded_shaders::model_taa_vert});
    case ShaderId::ModelTaaFragment:
        return std::as_bytes(std::span{embedded_shaders::model_taa_frag});
    case ShaderId::ShadowVertex:
        return std::as_bytes(std::span{embedded_shaders::shadow_vert});
    case ShaderId::ShadowFragment:
        return std::as_bytes(std::span{embedded_shaders::shadow_frag});
    }
    throw std::invalid_argument("Unknown embedded shader ID");
}
}
