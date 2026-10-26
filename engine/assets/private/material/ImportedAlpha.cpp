#include "ImportedAlpha.hpp"
#include "MaterialValidation.hpp"
#include "ResourcePath.hpp"
#include "image/ImageDecoder.hpp"
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace aeg::assets::detail {
namespace {
void validateCompositionImage(const ImageData& image) {
    constexpr std::uint32_t maximumDimension = 16384;
    constexpr std::uint64_t maximumPixels = 64ull * 1024 * 1024;
    const auto pixels = static_cast<std::uint64_t>(image.width) * image.height;
    if (!image.width || !image.height || image.width > maximumDimension || image.height > maximumDimension ||
        pixels > maximumPixels || image.rgba.size() != pixels * 4)
        throw std::runtime_error("Opacity composition image dimensions or RGBA byte count exceed supported limits");
}
std::size_t sampleOffset(const ImageData& image, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                         std::uint32_t height) {
    // Nearest source texel at each output texel's normalized UV center. Integer
    // arithmetic and bounded dimensions keep the rule deterministic and finite.
    const auto sx = (static_cast<std::uint64_t>(2) * x + 1) * image.width / (2ull * width);
    const auto sy = (static_cast<std::uint64_t>(2) * y + 1) * image.height / (2ull * height);
    return static_cast<std::size_t>((sy * image.width + sx) * 4);
}
ImageData combineOpacity(const ImageData& base, const ImageData& opacity) {
    validateCompositionImage(base);
    validateCompositionImage(opacity);
    ImageData result;
    result.width = std::max(base.width, opacity.width);
    result.height = std::max(base.height, opacity.height);
    const auto pixels = static_cast<std::uint64_t>(result.width) * result.height;
    if (pixels > 64ull * 1024 * 1024)
        throw std::runtime_error("Combined opacity texture exceeds the 64-million-pixel limit");
    result.rgba.resize(static_cast<std::size_t>(pixels * 4));
    for (std::uint32_t y = 0; y < result.height; ++y)
        for (std::uint32_t x = 0; x < result.width; ++x) {
            const auto destination = (static_cast<std::size_t>(y) * result.width + x) * 4;
            const auto color = sampleOffset(base, x, y, result.width, result.height);
            const auto mask = sampleOffset(opacity, x, y, result.width, result.height);
            std::copy_n(base.rgba.data() + color, 3, result.rgba.data() + destination);
            // map_d is scalar linear data from R, not its image's alpha channel.
            result.rgba[destination + 3] = static_cast<std::uint8_t>(
                (static_cast<unsigned>(base.rgba[color + 3]) * opacity.rgba[mask] + 127) / 255);
        }
    return result;
}
void applyOpacityTexture(const aiScene& scene, const aiMaterial& source, std::size_t materialIndex,
                         MaterialData& material, ModelData& model, bool unsupportedOptions) {
    aiString texture;
    aiTextureMapping mapping = aiTextureMapping_UV;
    unsigned uvIndex{};
    if (source.GetTexture(aiTextureType_OPACITY, 0, &texture, &mapping, &uvIndex) != AI_SUCCESS) return;
    const auto warn = [&](std::string message) {
        model.warnings.push_back("Opacity texture for material '" + material.name + "' " + std::move(message));
    };
    aiUVTransform transform;
    bool missingUV0 = false;
    for (unsigned i = 0; i < scene.mNumMeshes; ++i)
        if (scene.mMeshes[i]->mMaterialIndex == materialIndex && !scene.mMeshes[i]->HasTextureCoords(0))
            missingUV0 = true;
    if (mapping != aiTextureMapping_UV || uvIndex != 0 || missingUV0) {
        warn("requires unsupported texture coordinates; only existing UV0 is supported; keeping the base image");
        return;
    }
    if (unsupportedOptions || source.Get(AI_MATKEY_UVTRANSFORM(aiTextureType_OPACITY, 0), transform) == AI_SUCCESS) {
        warn("uses an unsupported UV transform or texture option; keeping the base image");
        return;
    }
    try {
        const auto opacity = readTexture(scene, texture, model.source.parent_path(), isGltf(model.source));
        auto combined = combineOpacity(material.texture, opacity);
        material.texture = std::move(combined);
        material.hasTexture = true;
    } catch (const std::exception& error) {
        warn("'" + std::string(texture.C_Str()) + "' unavailable; keeping the base image: " + error.what());
    }
}
AlphaMode inferAlpha(const MaterialData& material) {
    if (material.baseColor.w < 1) return AlphaMode::Blend;
    bool transparent = false;
    for (std::size_t i = 3; i < material.texture.rgba.size(); i += 4) {
        const auto alpha = material.texture.rgba[i];
        if (alpha > 0 && alpha < 255) return AlphaMode::Blend;
        transparent |= alpha == 0;
    }
    return transparent ? AlphaMode::Mask : AlphaMode::Opaque;
}
}
void applyImportedAlpha(const aiScene& scene, const aiMaterial& source, std::size_t materialIndex,
                        MaterialData& material, ModelData& model, bool pmx, bool unsupportedOpacityOptions) {
    material.alphaCutoff = pmx ? 0.1f : 0.5f;
    aiString alphaMode;
    if (source.Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS) {
        const std::string_view mode(alphaMode.C_Str());
        if (mode == "OPAQUE")
            material.alphaMode = AlphaMode::Opaque;
        else if (mode == "MASK") {
            material.alphaMode = AlphaMode::Mask;
            source.Get(AI_MATKEY_GLTF_ALPHACUTOFF, material.alphaCutoff);
        } else if (mode == "BLEND")
            material.alphaMode = AlphaMode::Blend;
        else
            throw std::runtime_error("Model contains an invalid alpha mode");
        // glTF publishes the same alpha in baseColor and OPACITY; do not square it.
        validateMaterialAlpha(material);
        return;
    }
    source.Get(AI_MATKEY_OPACITY, material.baseColor.w);
    validateMaterialAlpha(material);
    applyOpacityTexture(scene, source, materialIndex, material, model, unsupportedOpacityOptions);
    material.alphaMode = inferAlpha(material);
}
}
