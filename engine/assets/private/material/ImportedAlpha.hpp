#pragma once
#include <ModelData.hpp>
#include <cstddef>

struct aiMaterial;
struct aiScene;
namespace aeg::assets::detail {
// Resolve explicit glTF policy or infer generic imported opacity after base
// image decoding. OBJ options discarded by Assimp are diagnosed by the caller.
void applyImportedAlpha(const aiScene& scene, const aiMaterial& source, std::size_t materialIndex,
                        MaterialData& material, ModelData& model, bool pmx, bool unsupportedOpacityOptions);
}
