#pragma once
#include <ModelData.hpp>
#include <cstddef>

namespace aeg::assets {
// Changes only the selected material's alpha policy. Opacity is the base-color
// factor alpha, multiplied by sampled texture alpha for Mask/Blend and ignored
// by Opaque. Valid modes, finite opacity in [0,1], and finite cutoff >=0 are
// required. Failure preserves the complete model; success preserves geometry,
// textures, normal mapping, notices, and all other materials.
void setMaterialAlpha(ModelData& model, std::size_t materialIndex, AlphaMode mode, float opacity, float cutoff = 0.5f);
}
