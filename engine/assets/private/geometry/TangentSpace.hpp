#pragma once
#include <ModelData.hpp>

namespace aeg::assets::detail {
// Uses final, transformed positions and UV0. Opposite-handed and unusable
// triangle corners receive separate vertices. Returns false for any unusable
// face; those corners retain tangent.w == 0 for geometric-normal shading.
bool buildTangentSpace(MeshData& mesh);
}
