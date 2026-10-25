#pragma once
#include "vulkan/VulkanResources.hpp"
#include "SceneUniform.hpp"
#include <vector>

namespace aeg::render {
struct GpuMesh {
    detail::Buffer vertices, indices;
    std::uint32_t indexCount{}, material{};
};

struct GpuMaterial {
    detail::Image image;
    VkDescriptorSet descriptor{};
    MaterialPush parameters;
};

struct GpuModel {
    VkDevice device{};
    VkDescriptorPool pool{};
    std::vector<GpuMesh> meshes;
    std::vector<GpuMaterial> materials;
    ~GpuModel() {
        if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
    }
};
}
