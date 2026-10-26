#pragma once
#include <ModelData.hpp>

namespace aeg::assets::detail {
void validateMaterialAlpha(AlphaMode mode, float opacity, float cutoff);
void validateMaterialPbr(float metallic, float roughness);
inline void validateMaterialAlpha(const MaterialData& material) {
    validateMaterialAlpha(material.alphaMode, material.baseColor.w, material.alphaCutoff);
}
inline void validateMaterialPbr(const MaterialData& material) {
    validateMaterialPbr(material.metallicFactor, material.roughnessFactor);
}
}
