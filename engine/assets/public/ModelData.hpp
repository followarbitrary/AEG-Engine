#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>
#include <math.hpp>
#include <Bounds.hpp>

namespace aeg::assets {
struct Vertex {
    math::Vec3 position{};
    math::Vec3 normal{0, 1, 0};
    math::Vec2 uv{};
    // Final UV0 +U direction and handedness (+/-1); w=0 disables normal mapping.
    math::Vec4 tangent{0, 0, 0, 0};
};
static_assert(std::is_standard_layout_v<Vertex> && std::is_trivially_copyable_v<Vertex>);
static_assert(sizeof(Vertex) == 64 && alignof(Vertex) == 16);
static_assert(offsetof(Vertex, position) == 0 && offsetof(Vertex, normal) == 16);
static_assert(offsetof(Vertex, uv) == 32 && offsetof(Vertex, tangent) == 48);
struct ImageData {
    std::uint32_t width{1}, height{1};
    std::vector<std::uint8_t> rgba{255, 255, 255, 255};
};
enum class AlphaMode : std::uint32_t { Opaque, Mask, Blend };
struct MaterialData {
    std::string name;
    math::Vec4 baseColor{1};
    ImageData texture;
    bool hasTexture{false};
    AlphaMode alphaMode{AlphaMode::Opaque};
    float alphaCutoff{0.5f};
    ImageData normalTexture{1, 1, {128, 128, 255, 255}}; // Linear RGB tangent-space normals.
    bool hasNormalMap{false};
    float normalScale{1};            // Scale X/Y before normalization, including zero and negative values.
    std::string normalTextureSource; // Imported reference or resolved external image path; empty for memory images.
    std::filesystem::path baseTexturePath; // Resolved external base texture; empty for embedded textures.
    bool normalTextureDeclared{false};     // Native/sidecar bindings or explicit disable block filename guessing.
};
struct MeshData {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t materialIndex{0};
    bool hasUV0{true}; // Importers set this explicitly; procedural vertices conventionally provide UV0.
};
struct ModelData {
    std::string name;
    std::filesystem::path source;
    std::vector<MeshData> meshes;
    std::vector<MaterialData> materials;
    foundation::Bounds bounds;
    std::vector<std::string> warnings;
    [[nodiscard]] std::size_t vertexCount() const;
    [[nodiscard]] std::size_t triangleCount() const;
};
}
