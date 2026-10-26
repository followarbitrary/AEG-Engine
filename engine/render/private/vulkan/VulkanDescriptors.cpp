#include "VulkanRendererImpl.hpp"

namespace aeg::render {
void VulkanRenderer::Impl::createDescriptors() {
    VkDescriptorSetLayoutBinding uniform{};
    uniform.binding = 0;
    uniform.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniform.descriptorCount = 1;
    uniform.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 1;
    layout.pBindings = &uniform;
    check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &sceneLayout), "vkCreateDescriptorSetLayout(scene)");
    VkDescriptorSetLayoutBinding texture{};
    texture.binding = 0;
    texture.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture.descriptorCount = 1;
    texture.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layout.pBindings = &texture;
    check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &postSetLayout),
          "vkCreateDescriptorSetLayout(postprocess)");
    std::array materialTextures{texture, texture};
    materialTextures[1].binding = 1;
    layout.bindingCount = static_cast<std::uint32_t>(materialTextures.size());
    layout.pBindings = materialTextures.data();
    check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &materialLayout),
          "vkCreateDescriptorSetLayout(material)");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    check(vkCreateDescriptorPool(device, &pool, nullptr, &uniformPool), "vkCreateDescriptorPool(uniform)");
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = uniformPool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &sceneLayout;
    check(vkAllocateDescriptorSets(device, &allocate, &sceneDescriptor), "vkAllocateDescriptorSets(scene)");
    uniformBuffer = resources.buffer(sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    VkDescriptorBufferInfo buffer{uniformBuffer.handle, 0, sizeof(SceneUniform)};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = sceneDescriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &buffer;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128};
    pool.maxSets = 128;
    pool.pPoolSizes = &size;
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    check(vkCreateDescriptorPool(device, &pool, nullptr, &uiPool), "vkCreateDescriptorPool(ImGui)");
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.minLod = 0;
    info.maxLod = 0;
    info.maxAnisotropy = 1;
    check(vkCreateSampler(device, &info, nullptr, &sampler), "vkCreateSampler");
    info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(device, &info, nullptr, &viewportSampler), "vkCreateSampler(viewport)");
    info.magFilter = info.minFilter = VK_FILTER_NEAREST;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    check(vkCreateSampler(device, &info, nullptr, &pointSampler), "vkCreateSampler(point)");

    const std::array layouts{sceneLayout, materialLayout};
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MaterialPush)};
    VkPipelineLayoutCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineInfo.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
    pipelineInfo.pSetLayouts = layouts.data();
    pipelineInfo.pushConstantRangeCount = 1;
    pipelineInfo.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout(scene)");
    push.size = sizeof(glm::vec2);
    pipelineInfo.setLayoutCount = 1;
    pipelineInfo.pSetLayouts = &postSetLayout;
    check(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &postLayout), "vkCreatePipelineLayout(FXAA)");
    if (temporalSupported) {
        std::array<VkDescriptorSetLayoutBinding, 4> textures{};
        for (std::uint32_t binding = 0; binding < textures.size(); ++binding)
            textures[binding] = {binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                                 nullptr};
        layout.bindingCount = static_cast<std::uint32_t>(textures.size());
        layout.pBindings = textures.data();
        check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &temporalSetLayout),
              "vkCreateDescriptorSetLayout(TAA)");
        pipelineInfo.pSetLayouts = &temporalSetLayout;
        push.size = sizeof(TemporalPush);
        check(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &temporalLayout), "vkCreatePipelineLayout(TAA)");
    }
}
}
