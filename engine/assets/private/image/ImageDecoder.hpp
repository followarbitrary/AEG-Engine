#pragma once
#include <ModelData.hpp>
#include <filesystem>

struct aiScene;
struct aiString;

namespace aeg::assets::detail {
ImageData readTexture(const std::filesystem::path& path);
ImageData readTexture(const aiScene& scene, const aiString& name, const std::filesystem::path& directory, bool gltf,
                      std::filesystem::path* externalPath = nullptr);
}
