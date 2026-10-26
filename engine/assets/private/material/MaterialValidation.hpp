#pragma once
#include <ModelData.hpp>

namespace aeg::assets::detail {
void validateMaterialAlpha(AlphaMode mode, float opacity, float cutoff);
inline void validateMaterialAlpha(const MaterialData& material) {
    validateMaterialAlpha(material.alphaMode, material.baseColor.w, material.alphaCutoff);
}
}
