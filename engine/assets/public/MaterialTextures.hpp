#pragma once
#include <ModelData.hpp>
#include <cstddef>
#include <filesystem>

namespace aeg::assets {
// Once-per-import finishing step: discover undeclared external normal textures
// using shared naming rules, then prepare tangent frames. Preserves the model
// on failure; existing/native normal bindings take precedence over discovery.
void initializeModelMaterials(ModelData& model);

// Shared finishing step for every importer and procedural model. Prepares
// tangent frames for normal-mapped materials, preserving the model on failure.
void prepareModelMaterials(ModelData& model);

// Assigns/replaces only the selected material, preparing its affected meshes.
// The RGB image is linear tangent-space data. Scale affects X/Y before normalization.
// Failures leave materials, geometry and notices unchanged.
void setMaterialNormalMap(ModelData& model, std::size_t materialIndex, ImageData image, float scale = 1);

// Relative image paths are literal filesystem paths based on the model's
// directory (or cwd for models without a source). No URI percent decoding occurs.
void setMaterialNormalMap(ModelData& model, std::size_t materialIndex, const std::filesystem::path& imagePath,
                          float scale = 1);

// Clears only the selected material. Existing seam-split vertices are retained.
void clearMaterialNormalMap(ModelData& model, std::size_t materialIndex);
}
