#pragma once
#include <RhiBinding.hpp>
#include "SceneUniform.hpp"
#include <memory>
#include <vector>
namespace aeg::render {
struct GpuMesh {
    std::unique_ptr<rhi::Buffer> vertices, indices;
    std::uint32_t indexCount{}, material{};
    math::Vec3 center{};
};
struct GpuMaterial {
    std::unique_ptr<rhi::Texture> image, normalImage;
    MaterialPush parameters;
    std::unique_ptr<rhi::BindingSet> descriptor;
};
struct GpuModel {
    // Retain bounds computed from validated vertices rather than import metadata.
    math::Vec3 minimum{}, maximum{};
    std::vector<GpuMesh> meshes;
    std::vector<GpuMaterial> materials;
};
}
