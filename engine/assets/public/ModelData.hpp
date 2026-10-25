#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <Bounds.hpp>

namespace aeg::assets {
struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{0, 1, 0};
    glm::vec2 uv{};
};
struct ImageData {
    std::uint32_t width{1}, height{1};
    std::vector<std::uint8_t> rgba{255, 255, 255, 255};
};
struct MaterialData {
    std::string name;
    glm::vec4 baseColor{1};
    ImageData texture;
    bool hasTexture{false};
    bool alphaMasked{false};
    float alphaCutoff{0.5f};
};
struct MeshData {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t materialIndex{0};
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
