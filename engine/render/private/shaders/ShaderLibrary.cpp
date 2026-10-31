#include "ShaderLibrary.hpp"
#include <embedded_shaders/model.vert.spv.hpp>
#include <embedded_shaders/model.frag.spv.hpp>
#include <embedded_shaders/fullscreen.vert.spv.hpp>
#include <embedded_shaders/fxaa.frag.spv.hpp>
#include <embedded_shaders/taa.frag.spv.hpp>
#include <embedded_shaders/model_taa.vert.spv.hpp>
#include <embedded_shaders/model_taa.frag.spv.hpp>
#include <embedded_shaders/model_bindless.vert.spv.hpp>
#include <embedded_shaders/shadow.vert.spv.hpp>
#include <embedded_shaders/shadow.frag.spv.hpp>
#include <embedded_shaders/environment.comp.spv.hpp>
#include <embedded_shaders/irradiance.comp.spv.hpp>
#include <embedded_shaders/prefilter.comp.spv.hpp>
#include <embedded_shaders/brdf.comp.spv.hpp>
#include <embedded_shaders/tonemap.frag.spv.hpp>
#include <embedded_shaders/cull.comp.spv.hpp>
#include <embedded_shaders/model_bindless.frag.spv.hpp>
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
    case ShaderId::BindlessModelVertex:
        return std::as_bytes(std::span{embedded_shaders::model_bindless_vert});
    case ShaderId::ShadowVertex:
        return std::as_bytes(std::span{embedded_shaders::shadow_vert});
    case ShaderId::ShadowFragment:
        return std::as_bytes(std::span{embedded_shaders::shadow_frag});
    case ShaderId::EnvironmentCompute:
        return std::as_bytes(std::span{embedded_shaders::environment_comp});
    case ShaderId::IrradianceCompute:
        return std::as_bytes(std::span{embedded_shaders::irradiance_comp});
    case ShaderId::PrefilterCompute:
        return std::as_bytes(std::span{embedded_shaders::prefilter_comp});
    case ShaderId::BrdfCompute:
        return std::as_bytes(std::span{embedded_shaders::brdf_comp});
    case ShaderId::ToneMappingFragment:
        return std::as_bytes(std::span{embedded_shaders::tonemap_frag});
    case ShaderId::CullCompute:
        return std::as_bytes(std::span{embedded_shaders::cull_comp});
    case ShaderId::BindlessModelFragment:
        return std::as_bytes(std::span{embedded_shaders::model_bindless_frag});
    }
    throw std::invalid_argument("Unknown embedded shader ID");
}
}
