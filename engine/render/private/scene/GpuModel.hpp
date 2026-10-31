#pragma once
#include <RhiBinding.hpp>
#include "SceneUniform.hpp"
#include <array>
#include <memory>
#include <vector>
namespace aeg::render {
struct alignas(16) GpuCullBounds {
    math::Vec4 localMinimum{};
    math::Vec4 localMaximum{};
    std::array<std::uint32_t, 4> draw{}; // index count, first index, vertex offset, material index.
};
static_assert(sizeof(GpuCullBounds) == 48 && alignof(GpuCullBounds) == 16);
struct alignas(16) GpuCullPush {
    std::array<math::Vec4, 6> planes{};
    std::uint32_t meshCount{};
    std::array<std::uint32_t, 3> padding{};
};
static_assert(sizeof(GpuCullPush) == 112 && alignof(GpuCullPush) == 16);
struct GpuMesh {
    std::unique_ptr<rhi::Buffer> vertices, indices;
    std::uint32_t indexCount{}, material{};
    math::Vec3 center{};
    math::Vec3 minimum{}, maximum{};
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
    std::unique_ptr<rhi::Buffer> boundsBuffer;
    std::unique_ptr<rhi::Buffer> indirectBuffer;
    std::unique_ptr<rhi::BindingSet> cullDescriptor;
    std::unique_ptr<rhi::BindingSet> bindlessDescriptor;
    bool gpuDriven{};
};
}
