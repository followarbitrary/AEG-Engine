#pragma once
#include <vulkan/vulkan.h>
#include <utility>

namespace aeg::render::detail {
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
}
