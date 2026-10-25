#pragma once
#include <vulkan/vulkan.h>
#include <utility>

namespace aeg::render::detail {
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
}
