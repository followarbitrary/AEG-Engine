#include "VulkanBackendInternal.hpp"

namespace aeg::rhi::vulkan::detail {
VkShaderStageFlags shaderStages(ShaderStage stages) {
    constexpr auto valid = std::to_underlying(ShaderStage::Vertex | ShaderStage::Fragment | ShaderStage::Compute);
    if (std::to_underlying(stages) & ~valid) throw std::invalid_argument("Unknown RHI shader stage");
    VkShaderStageFlags flags{};
    if (contains(stages, ShaderStage::Vertex)) flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (contains(stages, ShaderStage::Fragment)) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (contains(stages, ShaderStage::Compute)) flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    return flags;
}
namespace {
VkDescriptorType descriptorType(BindingType type) {
    switch (type) {
    case BindingType::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::CombinedTextureSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case BindingType::StorageImage:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case BindingType::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    throw std::invalid_argument("Unknown RHI binding type");
}

std::uint32_t roundedDescriptorCount(std::uint32_t count) {
    if (!count) return 0;
    std::uint64_t value = 1;
    while (value < count) {
        if (value > std::numeric_limits<std::uint32_t>::max() / 2u)
            throw std::overflow_error("Descriptor pool size overflow");
        value <<= 1;
    }
    return static_cast<std::uint32_t>(value);
}

std::uint32_t pageCapacity(std::array<std::uint32_t, 4> key) {
    constexpr std::uint32_t requested = 64;
    std::uint32_t capacity = requested;
    for (auto descriptorCount : key) {
        if (descriptorCount)
            capacity = std::min(capacity, std::max<std::uint32_t>(1, std::numeric_limits<std::uint32_t>::max() /
                                                                         descriptorCount));
    }
    return capacity;
}
}
VulkanDescriptorPoolPage::~VulkanDescriptorPoolPage() {
    if (pool && owner && owner->handle) vkDestroyDescriptorPool(owner->handle, pool, nullptr);
}
VulkanBindingLayout::~VulkanBindingLayout() {
    if (handle) vkDestroyDescriptorSetLayout(owner->handle, handle, nullptr);
}
VulkanBindingSet::~VulkanBindingSet() {
    if (page && page->pool && handle) {
        vkFreeDescriptorSets(owner->handle, page->pool, 1, &handle);
        if (page->activeSets) --page->activeSets;
    }
}
VulkanPipelineLayout::~VulkanPipelineLayout() {
    if (handle) vkDestroyPipelineLayout(owner->handle, handle, nullptr);
}
std::unique_ptr<BindingLayout> VulkanDevice::createBindingLayout(const BindingLayoutDesc& desc) {
    auto result = std::make_unique<VulkanBindingLayout>(*this, desc);
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::set<std::uint32_t> indices;
    std::array<std::uint64_t, 4> descriptorCounts{};
    for (const auto& binding : desc.bindings) {
        if (!indices.insert(binding.binding).second || !shaderStages(binding.stages))
            throw std::invalid_argument("Invalid or duplicate RHI binding");
        if (contains(binding.stages, ShaderStage::Compute) && !caps.compute)
            throw std::invalid_argument("Compute binding visibility is unsupported");
        if (binding.type == BindingType::StorageImage && binding.stages != ShaderStage::Compute)
            throw std::invalid_argument("Storage image bindings require compute-only visibility");
        if (!binding.arraySize) throw std::invalid_argument("Descriptor array size must be nonzero");
        const auto type = static_cast<std::size_t>(binding.type);
        if (type >= descriptorCounts.size()) throw std::invalid_argument("Unknown RHI binding type");
        descriptorCounts[type] += binding.arraySize;
        bindings.push_back({binding.binding, descriptorType(binding.type), binding.arraySize, shaderStages(binding.stages), nullptr});
    }
    if (descriptorCounts[0] > properties.limits.maxDescriptorSetUniformBuffers ||
        descriptorCounts[1] > std::min(properties.limits.maxDescriptorSetSampledImages,
                                        properties.limits.maxDescriptorSetSamplers) ||
        descriptorCounts[2] > properties.limits.maxDescriptorSetStorageImages ||
        descriptorCounts[3] > properties.limits.maxDescriptorSetStorageBuffers)
        throw std::invalid_argument("Descriptor array count exceeds device limits");
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(handle, &info, nullptr, &result->handle), "vkCreateDescriptorSetLayout");
    return result;
}
std::unique_ptr<BindingSet> VulkanDevice::createBindingSet(BindingLayout& generic,
                                                           std::span<const BindingWrite> writes) {
    auto& layout = owned<VulkanBindingLayout>(*this, generic);
    auto result = std::make_unique<VulkanBindingSet>(*this, layout.desc);
    std::array<std::uint64_t, 4> counts{};
    for (const auto& binding : layout.desc.bindings) {
        const auto index = static_cast<std::size_t>(binding.type);
        if (index >= counts.size()) throw std::invalid_argument("Unknown RHI binding type");
        counts[index] += binding.arraySize;
    }
    std::array<std::uint32_t, 4> rounded{};
    for (std::size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] > std::numeric_limits<std::uint32_t>::max())
            throw std::overflow_error("Descriptor count overflow");
        rounded[i] = roundedDescriptorCount(static_cast<std::uint32_t>(counts[i]));
    }
    result->page = allocateDescriptorSet(layout.handle, rounded, result->handle);
    result->update(writes);
    return result;
}

VulkanDescriptorPoolPage* VulkanDevice::allocateDescriptorSet(VkDescriptorSetLayout layout,
                                                               std::array<std::uint32_t, 4> key,
                                                               VkDescriptorSet& output) {
    const auto tryPage = [&](VulkanDescriptorPoolPage& page) -> VkResult {
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = page.pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &layout;
        const auto result = vkAllocateDescriptorSets(handle, &allocate, &output);
        if (result == VK_SUCCESS) ++page.activeSets;
        return result;
    };

    for (auto& candidate : descriptorPages) {
        if (candidate->key != key || candidate->activeSets >= candidate->capacitySets) continue;
        const auto result = tryPage(*candidate);
        if (result == VK_SUCCESS) return candidate.get();
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
            check(result, "vkAllocateDescriptorSets");
    }

    auto page = std::make_unique<VulkanDescriptorPoolPage>(*this, key);
    page->capacitySets = pageCapacity(key);
    std::vector<VkDescriptorPoolSize> sizes;
    constexpr VkDescriptorType types[] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                          VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
    for (std::size_t i = 0; i < key.size(); ++i) {
        if (!key[i]) continue;
        const auto descriptorCount = static_cast<std::uint64_t>(key[i]) * page->capacitySets;
        if (descriptorCount > std::numeric_limits<std::uint32_t>::max())
            throw std::overflow_error("Descriptor pool size overflow");
        sizes.push_back({types[i], static_cast<std::uint32_t>(descriptorCount)});
    }
    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets = page->capacitySets;
    info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    check(vkCreateDescriptorPool(handle, &info, nullptr, &page->pool), "vkCreateDescriptorPool");
    const auto result = tryPage(*page);
    if (result != VK_SUCCESS) {
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
            check(result, "vkAllocateDescriptorSets");
        throw std::runtime_error("Descriptor pool page cannot allocate requested layout");
    }
    auto* outputPage = page.get();
    descriptorPages.push_back(std::move(page));
    return outputPage;
}
void VulkanBindingSet::update(std::span<const BindingWrite> writes) {
    std::vector<VkWriteDescriptorSet> native(writes.size());
    std::vector<VkDescriptorBufferInfo> buffers(writes.size());
    std::vector<VkDescriptorImageInfo> images(writes.size());
    std::set<std::pair<std::uint32_t, std::uint32_t>> indices;
    for (std::size_t i = 0; i < writes.size(); ++i) {
        const auto& write = writes[i];
        auto found =
            std::ranges::find_if(desc.bindings, [&](const auto& entry) { return entry.binding == write.binding; });
        if (found == desc.bindings.end() || !indices.insert({write.binding, write.arrayElement}).second)
            throw std::invalid_argument("Unknown or duplicate binding write");
        auto& output = native[i];
        output.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        output.dstSet = handle;
        output.dstBinding = write.binding;
        if (write.arrayElement >= found->arraySize) throw std::invalid_argument("Descriptor array element exceeds layout");
        output.dstArrayElement = write.arrayElement;
        output.descriptorCount = 1;
        output.descriptorType = descriptorType(found->type);
        if (found->type == BindingType::UniformBuffer) {
            if (!write.buffer || write.texture || write.sampler || write.storageMipLevel)
                throw std::invalid_argument("Uniform binding requires only a buffer");
            auto& buffer = owned<VulkanBuffer>(*owner, *write.buffer);
            if (!contains(buffer.desc.usage, BufferUsage::Uniform))
                throw std::invalid_argument("Buffer lacks uniform usage");
            buffers[i] = {buffer.handle, 0, buffer.desc.size};
            output.pBufferInfo = &buffers[i];
        } else if (found->type == BindingType::CombinedTextureSampler) {
            if (write.buffer || !write.texture || !write.sampler || write.storageMipLevel)
                throw std::invalid_argument("Texture binding requires a texture and sampler");
            auto& texture = owned<VulkanTexture>(*owner, *write.texture);
            auto& sampler = owned<VulkanSampler>(*owner, *write.sampler);
            if (!contains(texture.desc.usage, TextureUsage::Sampled) || texture.desc.samples != 1)
                throw std::invalid_argument("Texture is not a single-sample sampled image");
            images[i] = {sampler.handle, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            output.pImageInfo = &images[i];
        } else if (found->type == BindingType::StorageImage) {
            if (write.buffer || !write.texture || write.sampler)
                throw std::invalid_argument("Storage image binding requires only a texture");
            auto& texture = owned<VulkanTexture>(*owner, *write.texture);
            if (!contains(texture.desc.usage, TextureUsage::Storage) || texture.desc.samples != 1 ||
                write.storageMipLevel >= texture.storageViews.size())
                throw std::invalid_argument("Texture storage usage or mip is invalid");
            images[i] = {VK_NULL_HANDLE, texture.storageViews[write.storageMipLevel], VK_IMAGE_LAYOUT_GENERAL};
            output.pImageInfo = &images[i];
        } else {
            if (!write.buffer || write.texture || write.sampler || write.storageMipLevel)
                throw std::invalid_argument("Storage buffer binding requires only a buffer");
            auto& buffer = owned<VulkanBuffer>(*owner, *write.buffer);
            if (!contains(buffer.desc.usage, BufferUsage::Storage))
                throw std::invalid_argument("Buffer lacks storage usage");
            buffers[i] = {buffer.handle, 0, buffer.desc.size};
            output.pBufferInfo = &buffers[i];
        }
    }
    auto initialized = initializedBindings;
    initialized.insert(indices.begin(), indices.end());
    vkUpdateDescriptorSets(owner->handle, static_cast<std::uint32_t>(native.size()), native.data(), 0, nullptr);
    initializedBindings.swap(initialized);
}
std::unique_ptr<PipelineLayout> VulkanDevice::createPipelineLayout(const PipelineLayoutDesc& desc) {
    if (desc.sets.size() > properties.limits.maxBoundDescriptorSets ||
        desc.pushConstantBytes > properties.limits.maxPushConstantsSize || desc.pushConstantBytes % 4 ||
        (desc.pushConstantBytes && !shaderStages(desc.pushConstantStages)) ||
        (desc.pushConstantBytes && contains(desc.pushConstantStages, ShaderStage::Compute) && !caps.compute))
        throw std::invalid_argument("Invalid Vulkan pipeline layout description");
    auto result = std::make_unique<VulkanPipelineLayout>(*this, desc);
    std::vector<VkDescriptorSetLayout> sets;
    for (auto* set : desc.sets) {
        if (!set) throw std::invalid_argument("Null binding layout");
        sets.push_back(owned<VulkanBindingLayout>(*this, *set).handle);
    }
    VkPushConstantRange push{shaderStages(desc.pushConstantStages), 0, desc.pushConstantBytes};
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = static_cast<std::uint32_t>(sets.size());
    info.pSetLayouts = sets.data();
    info.pushConstantRangeCount = desc.pushConstantBytes ? 1u : 0u;
    info.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(handle, &info, nullptr, &result->handle), "vkCreatePipelineLayout");
    return result;
}
}
