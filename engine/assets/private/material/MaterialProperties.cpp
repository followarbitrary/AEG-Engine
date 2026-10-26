#include <MaterialProperties.hpp>
#include "MaterialValidation.hpp"
#include <cmath>
#include <stdexcept>

namespace aeg::assets {
namespace detail {
void validateMaterialAlpha(AlphaMode mode, float opacity, float cutoff) {
    switch (mode) {
    case AlphaMode::Opaque:
    case AlphaMode::Mask:
    case AlphaMode::Blend:
        break;
    default:
        throw std::runtime_error("Material contains an invalid alpha mode");
    }
    if (!std::isfinite(opacity) || opacity < 0 || opacity > 1)
        throw std::runtime_error("Material opacity must be finite and in [0,1]");
    if (!std::isfinite(cutoff) || cutoff < 0)
        throw std::runtime_error("Material alpha cutoff must be finite and nonnegative");
}
void validateMaterialPbr(float metallic, float roughness) {
    if (!std::isfinite(metallic) || metallic < 0 || metallic > 1)
        throw std::runtime_error("Material metallic factor must be finite and in [0,1]");
    if (!std::isfinite(roughness) || roughness < 0 || roughness > 1)
        throw std::runtime_error("Material roughness factor must be finite and in [0,1]");
}
}
void setMaterialAlpha(ModelData& model, std::size_t materialIndex, AlphaMode mode, float opacity, float cutoff) {
    if (materialIndex >= model.materials.size())
        throw std::runtime_error("Alpha properties reference an invalid material");
    detail::validateMaterialAlpha(mode, opacity, cutoff);
    auto& material = model.materials[materialIndex];
    material.alphaMode = mode;
    material.baseColor.w = opacity;
    material.alphaCutoff = cutoff;
}
void setMaterialPbr(ModelData& model, std::size_t materialIndex, float metallic, float roughness) {
    if (materialIndex >= model.materials.size())
        throw std::runtime_error("PBR properties reference an invalid material");
    detail::validateMaterialPbr(metallic, roughness);
    auto& material = model.materials[materialIndex];
    material.metallicFactor = metallic;
    material.roughnessFactor = roughness;
}
}
