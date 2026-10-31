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
    if (contains(usage, TextureUsage::Storage)) flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    return flags;
}
VkBufferUsageFlags bufferUsage(BufferUsage usage) {
    VkBufferUsageFlags flags{};
    if (contains(usage, BufferUsage::Vertex)) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (contains(usage, BufferUsage::Index)) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (contains(usage, BufferUsage::Uniform)) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (contains(usage, BufferUsage::Storage)) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (contains(usage, BufferUsage::Indirect)) flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (contains(usage, BufferUsage::CopySource)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (contains(usage, BufferUsage::CopyDestination)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}
VkImageAspectFlags imageAspect(Format format) {
    if (format == Format::D24UnormS8) return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    return isDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}
VulkanBuffer::~VulkanBuffer() {
    if (handle) vmaDestroyBuffer(owner->allocator, handle, allocation);
}
VulkanTexture::~VulkanTexture() {
    if (nonOwning) return;
    for (const auto storageView : storageViews)
        if (storageView) vkDestroyImageView(owner->handle, storageView, nullptr);
    if (view) vkDestroyImageView(owner->handle, view, nullptr);
    if (handle) vmaDestroyImage(owner->allocator, handle, allocation);
}
VulkanSampler::~VulkanSampler() {
    if (handle) vkDestroySampler(owner->handle, handle, nullptr);
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
    VmaAllocationCreateInfo allocation{};
    allocation.usage = desc.memory == MemoryUsage::Device ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                                                           : VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocation.requiredFlags = desc.memory == MemoryUsage::Device
                                   ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                                   : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (desc.memory != MemoryUsage::Device)
        allocation.flags = (desc.memory == MemoryUsage::Upload ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                                                                : VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT) |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo allocationInfo{};
    check(vmaCreateBuffer(allocator, &info, &allocation, &result->handle, &result->allocation, &allocationInfo),
          "vmaCreateBuffer");
    result->mapping = allocationInfo.pMappedData;
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
    const auto support = textureSupport(desc.format, desc.usage, desc.dimension);
    if (!support.supported || !(support.sampleMask & desc.samples) || desc.extent.width > support.maximumExtent.width ||
        desc.extent.height > support.maximumExtent.height)
        throw std::invalid_argument("Unsupported Vulkan texture description");
    VkImageFormatProperties limits{};
    const VkImageCreateFlags flags = desc.dimension == TextureDimension::Cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    check(vkGetPhysicalDeviceImageFormatProperties(physical, nativeFormat(desc.format), VK_IMAGE_TYPE_2D,
                                                   VK_IMAGE_TILING_OPTIMAL, imageUsage(desc.usage), flags, &limits),
          "vkGetPhysicalDeviceImageFormatProperties(texture)");
    if (desc.mipLevels > limits.maxMipLevels || textureLayerCount(desc) > limits.maxArrayLayers)
        throw std::invalid_argument("Texture mip or layer count exceeds device limit");
    auto result = std::make_unique<VulkanTexture>(*this, desc);
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = flags;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = {desc.extent.width, desc.extent.height, 1};
    info.mipLevels = desc.mipLevels;
    info.arrayLayers = textureLayerCount(desc);
    info.format = nativeFormat(desc.format);
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage = imageUsage(desc.usage);
    info.samples = static_cast<VkSampleCountFlagBits>(desc.samples);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocation.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    check(vmaCreateImage(allocator, &info, &allocation, &result->handle, &result->allocation, nullptr),
          "vmaCreateImage");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = result->handle;
    view.viewType = desc.dimension == TextureDimension::Cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    view.format = info.format;
    // Sampled depth/stencil descriptors select one aspect. Framebuffer attachments
    // ignore the view aspect mask; barriers still transition both aspects of D24S8.
    const VkImageAspectFlags viewAspect =
        isDepthFormat(desc.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange = {viewAspect, 0, desc.mipLevels, 0, textureLayerCount(desc)};
    check(vkCreateImageView(handle, &view, nullptr, &result->view), "vkCreateImageView");
    if (contains(desc.usage, TextureUsage::Storage)) {
        result->storageViews.resize(desc.mipLevels, VK_NULL_HANDLE);
        view.viewType = desc.dimension == TextureDimension::Cube ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        view.subresourceRange.levelCount = 1;
        for (std::uint32_t mip = 0; mip < desc.mipLevels; ++mip) {
            view.subresourceRange.baseMipLevel = mip;
            check(vkCreateImageView(handle, &view, nullptr, &result->storageViews[mip]), "vkCreateImageView(storage)");
        }
    }
    return result;
}
std::unique_ptr<Texture> VulkanDevice::uploadTexture(const TextureDesc& description,
                                                     std::span<const std::byte> pixels) {
    validate(description);
    if ((description.format != Format::Rgba8Unorm && description.format != Format::Rgba8Srgb &&
         description.format != Format::Rgba32Float) ||
        description.samples != 1 || description.mipLevels != 1 || !contains(description.usage, TextureUsage::Sampled))
        throw std::invalid_argument("Texture upload requires sampled single-mip RGBA8 or RGBA32F faces");
    const auto pixelCount = static_cast<std::uint64_t>(description.extent.width) * description.extent.height;
    const auto pixelStride = formatByteSize(description.format) * textureLayerCount(description);
    if (pixelCount > std::numeric_limits<std::size_t>::max() / pixelStride || pixelCount * pixelStride != pixels.size())
        throw std::invalid_argument("Texture upload byte count mismatch");
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
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, textureLayerCount(desc)};
        copy.imageExtent = {desc.extent.width, desc.extent.height, 1};
        vkCmdCopyBufferToImage(commands.handle, buffer.handle, texture.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &copy);
        commands.transition(texture, ResourceState::CopyDestination, ResourceState::Sampled);
    });
    return result;
}
std::unique_ptr<Sampler> VulkanDevice::createSampler(const SamplerDesc& desc) {
    validate(desc);
    auto result = std::make_unique<VulkanSampler>(*this);
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = info.minFilter = desc.filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.mipmapMode = desc.filter == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = info.addressModeW =
        desc.address == AddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = desc.addressV.value_or(desc.address) == AddressMode::Repeat
                            ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                            : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxAnisotropy = 1;
    info.maxLod = desc.maxLod;
    check(vkCreateSampler(handle, &info, nullptr, &result->handle), "vkCreateSampler");
    return result;
}
TextureReadback VulkanDevice::readback(Texture& generic, ResourceState currentState, TextureSubresource subresource) {
    auto& texture = owned<VulkanTexture>(*this, generic);
    const auto& desc = texture.desc;
    if (isDepthFormat(desc.format) || desc.samples != 1 || !contains(desc.usage, TextureUsage::CopySource) ||
        currentState == ResourceState::Undefined)
        throw std::invalid_argument("Readback requires an initialized single-sample color copy source");
    const auto extent = mipExtent(desc, subresource.mipLevel);
    if (subresource.layer >= textureLayerCount(desc)) throw std::invalid_argument("Readback layer is out of range");
    const auto pixelCount = static_cast<std::uint64_t>(extent.width) * extent.height;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / formatByteSize(desc.format))
        throw std::length_error("Texture readback is too large");
    const auto byteCount = pixelCount * formatByteSize(desc.format);
    waitIdle();
    auto staging =
        createBuffer({static_cast<std::size_t>(byteCount), BufferUsage::CopyDestination, MemoryUsage::Readback});
    immediate([&](VulkanCommands& commands) {
        commands.transition(texture, currentState, ResourceState::CopySource);
        commands.copyTextureToBuffer(texture, *staging, subresource);
        commands.transition(texture, ResourceState::CopySource, currentState);
    });
    TextureReadback result{extent, desc.format, {}};
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
