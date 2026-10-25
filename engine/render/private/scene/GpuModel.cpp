#include "VulkanRendererImpl.hpp"

namespace aeg::render {
void VulkanRenderer::Impl::uploadModel(const ModelData& source) {
    if (source.meshes.empty()) throw std::runtime_error("Model has no renderable meshes");
    check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(model)");
    // Build a complete replacement first. Allocation errors preserve the old GPU model.
    auto replacement = std::make_unique<GpuModel>();
    replacement->device = device;
    const auto materialCount = std::max<std::size_t>(1, source.materials.size());
    if (materialCount > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("Too many materials");
    const auto count = static_cast<std::uint32_t>(materialCount);
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = count;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    check(vkCreateDescriptorPool(device, &pool, nullptr, &replacement->pool), "vkCreateDescriptorPool(materials)");
    replacement->materials.reserve(materialCount);
    const MaterialData fallback{};
    for (std::size_t i = 0; i < materialCount; ++i) {
        const auto& material = source.materials.empty() ? fallback : source.materials[i];
        GpuMaterial gpu;
        gpu.image = resources.texture(material.texture);
        gpu.parameters.color = material.baseColor;
        gpu.parameters.alphaMasked = material.alphaMasked ? 1u : 0u;
        gpu.parameters.alphaCutoff = material.alphaCutoff;
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = replacement->pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &materialLayout;
        check(vkAllocateDescriptorSets(device, &allocate, &gpu.descriptor), "vkAllocateDescriptorSets(material)");
        VkDescriptorImageInfo image{sampler, gpu.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = gpu.descriptor;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        replacement->materials.push_back(std::move(gpu));
    }
    replacement->meshes.reserve(source.meshes.size());
    for (const auto& mesh : source.meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty() ||
            mesh.indices.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Invalid or oversized mesh");
        if (mesh.materialIndex >= materialCount || mesh.indices.size() % 3 ||
            std::ranges::any_of(mesh.indices, [&](auto index) { return index >= mesh.vertices.size(); }))
            throw std::runtime_error("Invalid mesh indices or material");
        GpuMesh gpu;
        gpu.vertices = resources.upload<Vertex>(mesh.vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        gpu.indices = resources.upload<std::uint32_t>(mesh.indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        gpu.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        gpu.material = mesh.materialIndex;
        replacement->meshes.push_back(std::move(gpu));
    }
    model = std::move(replacement);
    resetTemporalHistory();
}
}
