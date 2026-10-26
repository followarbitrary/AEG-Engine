#pragma once
#include "VulkanBuffer.hpp"
#include "VulkanImage.hpp"
#include <ModelData.hpp>
#include <cstring>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>

namespace aeg::render::detail {
inline void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) + ")");
}

void barrier(VkCommandBuffer command, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
             VkAccessFlags sourceAccess, VkAccessFlags destinationAccess, VkPipelineStageFlags sourceStage,
             VkPipelineStageFlags destinationStage);

struct DeviceResources {
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    VkCommandPool commandPool{};

    std::uint32_t memoryType(std::uint32_t mask, VkMemoryPropertyFlags properties) const;
    Buffer buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                  bool map = false) const;
    Image image(std::uint32_t width, std::uint32_t height, VkFormat format, VkImageUsageFlags usage,
                VkImageAspectFlags aspect, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) const;
    void immediate(const std::function<void(VkCommandBuffer)>& record) const;
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
    Image texture(const assets::ImageData& pixels, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB) const;
};
}
