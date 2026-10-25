#pragma once
#include <ModelData.hpp>
#include <filesystem>

struct aiScene;
struct aiString;

namespace aeg::assets::detail {
ImageData readTexture(const aiScene& scene, const aiString& name,
                      const std::filesystem::path& directory, bool gltf);
}
