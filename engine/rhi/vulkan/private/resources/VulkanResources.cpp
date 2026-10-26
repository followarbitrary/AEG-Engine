#include "VulkanBackendInternal.hpp"

namespace aeg::rhi::vulkan::detail {
VkFormat nativeFormat(Format format) {
    switch (format) {
    case Format::Rgba8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::Rgba8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::Bgra8Unorm:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::Bgra8Srgb:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::Rgba16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::Rgba32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::R32Float:
        return VK_FORMAT_R32_SFLOAT;
    case Format::D32Float:
        return VK_FORMAT_D32_SFLOAT;
    case Format::D24UnormS8:
        return VK_FORMAT_D24_UNORM_S8_UINT;
    case Format::D16Unorm:
        return VK_FORMAT_D16_UNORM;
    case Format::Undefined:
        return VK_FORMAT_UNDEFINED;
    }
    throw std::invalid_argument("Unknown RHI format");
}
Format genericFormat(VkFormat format) {
    for (auto candidate :
         {Format::Rgba8Unorm, Format::Rgba8Srgb, Format::Bgra8Unorm, Format::Bgra8Srgb, Format::Rgba16Float,
          Format::Rgba32Float, Format::R32Float, Format::D32Float, Format::D24UnormS8, Format::D16Unorm})
        if (nativeFormat(candidate) == format) return candidate;
    throw std::runtime_error("Surface format is not represented by RHI");
}
VkImageUsageFlags imageUsage(TextureUsage usage) {
    VkImageUsageFlags flags{};
    if (contains(usage, TextureUsage::Sampled)) flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (contains(usage, TextureUsage::ColorAttachment)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (contains(usage, TextureUsage::DepthAttachment)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (contains(usage, TextureUsage::CopySource)) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (contains(usage, TextureUsage::CopyDestination)) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return flags;
}
VkBufferUsageFlags bufferUsage(BufferUsage usage) {
    VkBufferUsageFlags flags{};
    if (contains(usage, BufferUsage::Vertex)) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (contains(usage, BufferUsage::Index)) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (contains(usage, BufferUsage::Uniform)) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (contains(usage, BufferUsage::CopySource)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (contains(usage, BufferUsage::CopyDestination)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}
VkImageAspectFlags imageAspect(Format format) {
    if (format == Format::D24UnormS8) return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    return isDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}
VulkanBuffer::~VulkanBuffer() {
    if (mapping) vkUnmapMemory(owner->handle, memory);
    if (handle) vkDestroyBuffer(owner->handle, handle, nullptr);
    if (memory) vkFreeMemory(owner->handle, memory, nullptr);
}
VulkanTexture::~VulkanTexture() {
    if (view) vkDestroyImageView(owner->handle, view, nullptr);
    if (handle) vkDestroyImage(owner->handle, handle, nullptr);
    if (memory) vkFreeMemory(owner->handle, memory, nullptr);
}
VulkanSampler::~VulkanSampler() {
    if (handle) vkDestroySampler(owner->handle, handle, nullptr);
}
std::uint32_t VulkanDevice::memoryType(std::uint32_t mask, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties info{};
    vkGetPhysicalDeviceMemoryProperties(physical, &info);
    for (std::uint32_t i = 0; i < info.memoryTypeCount; ++i)
        if ((mask & (1u << i)) && (info.memoryTypes[i].propertyFlags & flags) == flags) return i;
    throw std::runtime_error("No compatible Vulkan memory type");
}
std::unique_ptr<Buffer> VulkanDevice::createBuffer(const BufferDesc& desc) {
    validate(desc);
    if (contains(desc.usage, BufferUsage::Uniform) && desc.size > properties.limits.maxUniformBufferRange)
        throw std::invalid_argument("Uniform buffer exceeds device limit");
    auto result = std::make_unique<VulkanBuffer>(*this, desc);
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = desc.size;
    info.usage = bufferUsage(desc.usage);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(handle, &info, nullptr, &result->handle), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(handle, result->handle, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType(
        requirements.memoryTypeBits, desc.memory == MemoryUsage::Device
                                         ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                                         : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(handle, &allocation, nullptr, &result->memory), "vkAllocateMemory(buffer)");
    check(vkBindBufferMemory(handle, result->handle, result->memory, 0), "vkBindBufferMemory");
    if (desc.memory != MemoryUsage::Device)
        check(vkMapMemory(handle, result->memory, 0, desc.size, 0, &result->mapping), "vkMapMemory");
    return result;
}
std::unique_ptr<Buffer> VulkanDevice::uploadBuffer(std::span<const std::byte> bytes, BufferUsage usage) {
    auto staging = createBuffer({bytes.size(), BufferUsage::CopySource, MemoryUsage::Upload});
    std::memcpy(staging->mapped().data(), bytes.data(), bytes.size());
    auto result = createBuffer({bytes.size(), usage | BufferUsage::CopyDestination, MemoryUsage::Device});
    immediate([&](VulkanCommands& commands) { commands.copyBuffer(*staging, *result, bytes.size()); });
    return result;
}
std::unique_ptr<Texture> VulkanDevice::createTexture(const TextureDesc& desc) {
    validate(desc);
    const auto support = textureSupport(desc.format, desc.usage);
    if (!support.supported || !(support.sampleMask & desc.samples) || desc.extent.width > support.maximumExtent.width ||
        desc.extent.height > support.maximumExtent.height)
        throw std::invalid_argument("Unsupported Vulkan texture description");
    auto result = std::make_unique<VulkanTexture>(*this, desc);
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = {desc.extent.width, desc.extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.format = nativeFormat(desc.format);
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = imageUsage(desc.usage);
    info.samples = static_cast<VkSampleCountFlagBits>(desc.samples);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateImage(handle, &info, nullptr, &result->handle), "vkCreateImage");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(handle, result->handle, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(handle, &allocation, nullptr, &result->memory), "vkAllocateMemory(image)");
    check(vkBindImageMemory(handle, result->handle, result->memory, 0), "vkBindImageMemory");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = result->handle;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = info.format;
    view.subresourceRange = {imageAspect(desc.format), 0, 1, 0, 1};
    check(vkCreateImageView(handle, &view, nullptr, &result->view), "vkCreateImageView");
    return result;
}
std::unique_ptr<Texture> VulkanDevice::uploadTexture(const TextureDesc& description,
                                                     std::span<const std::byte> pixels) {
    validate(description);
    if ((description.format != Format::Rgba8Unorm && description.format != Format::Rgba8Srgb) ||
        description.samples != 1 || !contains(description.usage, TextureUsage::Sampled))
        throw std::invalid_argument("Texture upload requires a sampled RGBA8 single-sample texture");
    const auto bytes = static_cast<std::uint64_t>(description.extent.width) * description.extent.height * 4;
    if (bytes != pixels.size()) throw std::invalid_argument("RGBA8 texture upload byte count mismatch");
    auto desc = description;
    desc.usage = desc.usage | TextureUsage::CopyDestination;
    auto staging = createBuffer({pixels.size(), BufferUsage::CopySource, MemoryUsage::Upload});
    std::memcpy(staging->mapped().data(), pixels.data(), pixels.size());
    auto result = createTexture(desc);
    auto& texture = owned<VulkanTexture>(*this, *result);
    auto& buffer = owned<VulkanBuffer>(*this, *staging);
    immediate([&](VulkanCommands& commands) {
        commands.transition(texture, ResourceState::Undefined, ResourceState::CopyDestination);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {desc.extent.width, desc.extent.height, 1};
        vkCmdCopyBufferToImage(commands.handle, buffer.handle, texture.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &copy);
        commands.transition(texture, ResourceState::CopyDestination, ResourceState::Sampled);
    });
    return result;
}
std::unique_ptr<Sampler> VulkanDevice::createSampler(const SamplerDesc& desc) {
    auto result = std::make_unique<VulkanSampler>(*this);
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = info.minFilter = desc.filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.mipmapMode = desc.filter == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = info.addressModeV = info.addressModeW =
        desc.address == AddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxAnisotropy = 1;
    info.maxLod = 0;
    check(vkCreateSampler(handle, &info, nullptr, &result->handle), "vkCreateSampler");
    return result;
}
TextureReadback VulkanDevice::readback(Texture& generic, ResourceState currentState) {
    auto& texture = owned<VulkanTexture>(*this, generic);
    const auto& desc = texture.desc;
    if (isDepthFormat(desc.format) || desc.samples != 1 || !contains(desc.usage, TextureUsage::CopySource) ||
        currentState == ResourceState::Undefined)
        throw std::invalid_argument("Readback requires an initialized single-sample color copy source");
    const std::uint64_t byteCount =
        static_cast<std::uint64_t>(desc.extent.width) * desc.extent.height * formatByteSize(desc.format);
    if (byteCount > std::numeric_limits<std::size_t>::max()) throw std::length_error("Texture readback is too large");
    waitIdle();
    auto staging =
        createBuffer({static_cast<std::size_t>(byteCount), BufferUsage::CopyDestination, MemoryUsage::Readback});
    immediate([&](VulkanCommands& commands) {
        commands.transition(texture, currentState, ResourceState::CopySource);
        commands.copyTextureToBuffer(texture, *staging);
        commands.transition(texture, ResourceState::CopySource, currentState);
    });
    TextureReadback result{desc.extent, desc.format, {}};
    const auto mapped = staging->mapped();
    result.pixels.assign(mapped.begin(), mapped.end());
    return result;
}
VkImageView nativeImageView(Device& device, Texture& texture) {
    return owned<VulkanTexture>(nativeDevice(device), texture).view;
}
VkSampler nativeSampler(Device& device, Sampler& sampler) {
    return owned<VulkanSampler>(nativeDevice(device), sampler).handle;
}
}
