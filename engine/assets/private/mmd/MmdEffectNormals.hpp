#pragma once
#include <ModelData.hpp>

namespace aeg::assets::detail {
// Converts MME's per-subset effect resources into ordinary engine materials.
void applyMmdEffectNormals(ModelData& model);
}
