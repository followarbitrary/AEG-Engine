#pragma once
#include <vulkan/vulkan.h>
#include <aeg/Model.hpp>
#include <cstring>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace aeg::detail {
inline void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) + ")");
}

struct Buffer {
    VkDevice device{};
    VkBuffer handle{};
    VkDeviceMemory memory{};
    VkDeviceSize size{};
    void* mapped{};
    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept { swap(other); }
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            reset();
            swap(other);
        }
        return *this;
    }
    ~Buffer() { reset(); }
    void swap(Buffer& other) noexcept {
        std::swap(device, other.device);
        std::swap(handle, other.handle);
        std::swap(memory, other.memory);
        std::swap(size, other.size);
        std::swap(mapped, other.mapped);
    }
    void reset() noexcept {
        if (mapped) vkUnmapMemory(device, memory);
        if (handle) vkDestroyBuffer(device, handle, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        mapped = nullptr;
        handle = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        size = 0;
    }
};

struct Image {
    VkDevice device{};
    VkImage handle{};
    VkDeviceMemory memory{};
    VkImageView view{};
    Image() = default;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& other) noexcept { swap(other); }
    Image& operator=(Image&& other) noexcept {
        if (this != &other) {
            reset();
            swap(other);
        }
        return *this;
    }
    ~Image() { reset(); }
    void swap(Image& other) noexcept {
        std::swap(device, other.device);
        std::swap(handle, other.handle);
        std::swap(memory, other.memory);
        std::swap(view, other.view);
    }
    void reset() noexcept {
        if (view) vkDestroyImageView(device, view, nullptr);
        if (handle) vkDestroyImage(device, handle, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        view = VK_NULL_HANDLE;
        handle = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
    }
};

inline void barrier(VkCommandBuffer command, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                    VkAccessFlags sourceAccess, VkAccessFlags destinationAccess, VkPipelineStageFlags sourceStage,
                    VkPipelineStageFlags destinationStage) {
    VkImageMemoryBarrier info{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    info.srcAccessMask = sourceAccess;
    info.dstAccessMask = destinationAccess;
    info.oldLayout = oldLayout;
    info.newLayout = newLayout;
    info.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    info.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    info.image = image;
    info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &info);
}

// Non-owning device context. Every allocation returned from it owns its handles.
struct DeviceResources {
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    VkCommandPool commandPool{};

    std::uint32_t memoryType(std::uint32_t mask, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties info{};
        vkGetPhysicalDeviceMemoryProperties(physical, &info);
        for (std::uint32_t i = 0; i < info.memoryTypeCount; ++i)
            if ((mask & (1u << i)) && (info.memoryTypes[i].propertyFlags & properties) == properties) return i;
        throw std::runtime_error("No compatible Vulkan memory type");
    }
    Buffer buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                  bool map = false) const {
        if (!size) throw std::runtime_error("Cannot allocate an empty GPU buffer");
        Buffer result;
        result.device = device;
        result.size = size;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device, &info, nullptr, &result.handle), "vkCreateBuffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, result.handle, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, properties);
        check(vkAllocateMemory(device, &allocation, nullptr, &result.memory), "vkAllocateMemory(buffer)");
        check(vkBindBufferMemory(device, result.handle, result.memory, 0), "vkBindBufferMemory");
        if (map) check(vkMapMemory(device, result.memory, 0, size, 0, &result.mapped), "vkMapMemory");
        return result;
    }
    Image image(std::uint32_t width, std::uint32_t height, VkFormat format, VkImageUsageFlags usage,
                VkImageAspectFlags aspect, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) const {
        Image result;
        result.device = device;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = format;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = usage;
        info.samples = samples;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateImage(device, &info, nullptr, &result.handle), "vkCreateImage");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, result.handle, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &result.memory), "vkAllocateMemory(image)");
        check(vkBindImageMemory(device, result.handle, result.memory, 0), "vkBindImageMemory");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = result.handle;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {aspect, 0, 1, 0, 1};
        check(vkCreateImageView(device, &view, nullptr, &result.view), "vkCreateImageView");
        return result;
    }
    void immediate(const std::function<void(VkCommandBuffer)>& record) const {
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        VkCommandBuffer command{};
        check(vkAllocateCommandBuffers(device, &allocation, &command), "vkAllocateCommandBuffers(upload)");
        try {
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer(upload)");
            record(command);
            check(vkEndCommandBuffer(command), "vkEndCommandBuffer(upload)");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &command;
            check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit(upload)");
            check(vkQueueWaitIdle(queue), "vkQueueWaitIdle(upload)");
        } catch (...) {
            vkFreeCommandBuffers(device, commandPool, 1, &command);
            throw;
        }
        vkFreeCommandBuffers(device, commandPool, 1, &command);
    }
    template <class T> Buffer upload(std::span<const T> data, VkBufferUsageFlags usage) const {
        const auto bytes = static_cast<VkDeviceSize>(data.size_bytes());
        auto staging = buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
        std::memcpy(staging.mapped, data.data(), data.size_bytes());
        auto result = buffer(bytes, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        immediate([&](VkCommandBuffer command) {
            VkBufferCopy copy{0, 0, bytes};
            vkCmdCopyBuffer(command, staging.handle, result.handle, 1, &copy);
            VkBufferMemoryBarrier memory{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            memory.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            memory.dstAccessMask = (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ? VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
                                                                               : VK_ACCESS_INDEX_READ_BIT;
            memory.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            memory.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            memory.buffer = result.handle;
            memory.offset = 0;
            memory.size = bytes;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0,
                                 nullptr, 1, &memory, 0, nullptr);
        });
        return result;
    }
    Image texture(const ImageData& pixels) const {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        const auto bytes = static_cast<std::uint64_t>(pixels.width) * pixels.height * 4;
        if (!pixels.width || !pixels.height || pixels.width > properties.limits.maxImageDimension2D ||
            pixels.height > properties.limits.maxImageDimension2D || bytes != pixels.rgba.size())
            throw std::runtime_error("Invalid or oversized material texture");
        auto staging = buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
        std::memcpy(staging.mapped, pixels.rgba.data(), pixels.rgba.size());
        auto result = image(pixels.width, pixels.height, VK_FORMAT_R8G8B8A8_SRGB,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        immediate([&](VkCommandBuffer command) {
            barrier(command, result.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {pixels.width, pixels.height, 1};
            vkCmdCopyBufferToImage(command, staging.handle, result.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &copy);
            barrier(command, result.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        });
        return result;
    }
};
}
