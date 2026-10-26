#include <MaterialTextures.hpp>
#include <Utf8Path.hpp>
#include "geometry/TangentSpace.hpp"
#include "image/ImageDecoder.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace aeg::assets {
namespace {
void validateIndex(const ModelData& model, std::size_t index) {
    if (index >= model.materials.size()) throw std::runtime_error("Normal texture references an invalid material");
}
void validateImage(const ImageData& image) {
    const auto pixels = static_cast<std::uint64_t>(image.width) * image.height;
    if (!pixels || pixels > std::numeric_limits<std::size_t>::max() / 4 || image.rgba.size() != pixels * 4)
        throw std::runtime_error("Normal texture dimensions or RGBA byte count are invalid");
}
bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
void validateMesh(const MeshData& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 ||
        mesh.vertices.size() >= std::numeric_limits<std::uint32_t>::max() ||
        mesh.indices.size() > std::numeric_limits<std::uint32_t>::max() ||
        std::any_of(mesh.indices.begin(), mesh.indices.end(), [&](auto i) { return i >= mesh.vertices.size(); }))
        throw std::runtime_error("Normal-mapped mesh contains invalid triangle indices");
    for (const auto& vertex : mesh.vertices)
        if (!finite(vertex.position) || !finite(vertex.normal) || !std::isfinite(vertex.uv.x) ||
            !std::isfinite(vertex.uv.y) || !(glm::length(glm::dvec3(vertex.normal)) > 1e-12))
            throw std::runtime_error("Normal-mapped mesh contains invalid geometry");
}
struct PreparedMeshes {
    std::vector<std::pair<std::size_t, MeshData>> meshes;
    std::vector<std::string> warnings;
};
PreparedMeshes prepare(const ModelData& model, std::optional<std::size_t> selected,
                       const MaterialData* replacement = nullptr) {
    PreparedMeshes prepared;
    prepared.warnings = model.warnings;
    auto warn = [&](std::string warning) {
        if (std::find(prepared.warnings.begin(), prepared.warnings.end(), warning) == prepared.warnings.end())
            prepared.warnings.push_back(std::move(warning));
    };
    for (std::size_t i = 0; i < model.materials.size(); ++i) {
        if (selected && *selected != i) continue;
        const auto& material = replacement ? *replacement : model.materials[i];
        if (material.hasNormalMap) {
            validateImage(material.normalTexture);
            if (!std::isfinite(material.normalScale)) throw std::runtime_error("Normal texture scale is not finite");
        }
    }
    for (std::size_t i = 0; i < model.meshes.size(); ++i) {
        const auto& mesh = model.meshes[i];
        validateIndex(model, mesh.materialIndex);
        if (selected && *selected != mesh.materialIndex) continue;
        const auto& material = replacement ? *replacement : model.materials[mesh.materialIndex];
        if (!material.hasNormalMap) continue;
        validateMesh(mesh);
        auto candidate = mesh;
        if (!candidate.hasUV0) {
            for (auto& vertex : candidate.vertices) vertex.tangent = glm::vec4(0);
            warn("Mesh '" + mesh.name + "' has no UV0 for its normal texture; keeping vertex normals");
        } else if (!detail::buildTangentSpace(candidate)) {
            warn("Mesh '" + mesh.name +
                 "' has degenerate UV/geometry normal-map bases; those faces use vertex normals");
        }
        prepared.meshes.emplace_back(i, std::move(candidate));
    }
    return prepared;
}
void commit(ModelData& model, PreparedMeshes prepared) noexcept {
    static_assert(std::is_nothrow_move_assignable_v<MeshData>);
    for (auto& [index, mesh] : prepared.meshes) model.meshes[index] = std::move(mesh);
    model.warnings.swap(prepared.warnings);
}
void assign(ModelData& model, std::size_t index, ImageData image, float scale, std::string source) {
    validateIndex(model, index);
    validateImage(image);
    if (!std::isfinite(scale)) throw std::runtime_error("Normal texture scale is not finite");
    auto material = model.materials[index];
    material.normalTexture = std::move(image);
    material.hasNormalMap = true;
    material.normalScale = scale;
    material.normalTextureSource = std::move(source);
    auto prepared = prepare(model, index, &material);
    static_assert(std::is_nothrow_move_assignable_v<MaterialData>);
    model.materials[index] = std::move(material);
    commit(model, std::move(prepared));
}
}

void prepareModelMaterials(ModelData& model) { commit(model, prepare(model, std::nullopt)); }

void setMaterialNormalMap(ModelData& model, std::size_t materialIndex, ImageData image, float scale) {
    assign(model, materialIndex, std::move(image), scale, {});
}

void setMaterialNormalMap(ModelData& model, std::size_t materialIndex, const std::filesystem::path& imagePath,
                          float scale) {
    validateIndex(model, materialIndex);
    if (imagePath.empty()) throw std::runtime_error("Normal texture path is empty");
    const auto candidate = imagePath.is_absolute() ? imagePath : model.source.parent_path() / imagePath;
    std::error_code error;
    const auto path = std::filesystem::absolute(candidate, error).lexically_normal();
    if (error) throw std::runtime_error("Normal texture path cannot be resolved: " + foundation::pathToUtf8(imagePath));
    const auto source = foundation::pathToUtf8(path);
    ImageData image;
    try {
        image = detail::readTexture(path);
    } catch (const std::exception& failure) {
        throw std::runtime_error("Cannot load normal texture '" + source + "': " + failure.what());
    }
    assign(model, materialIndex, std::move(image), scale, source);
}

void clearMaterialNormalMap(ModelData& model, std::size_t materialIndex) {
    validateIndex(model, materialIndex);
    auto material = model.materials[materialIndex];
    material.normalTexture = {1, 1, {128, 128, 255, 255}};
    material.hasNormalMap = false;
    material.normalScale = 1;
    material.normalTextureSource.clear();
    static_assert(std::is_nothrow_move_assignable_v<MaterialData>);
    model.materials[materialIndex] = std::move(material);
}
}
