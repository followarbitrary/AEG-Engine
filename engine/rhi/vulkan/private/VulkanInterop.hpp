#pragma once
#include <RhiSwapchain.hpp>
#include <vulkan/vulkan.h>
namespace aeg::rhi::vulkan::detail {
struct NativeContext {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue graphicsQueue{};
    std::uint32_t graphicsFamily{};
};
NativeContext nativeContext(Device& device);
VkRenderPass nativeGuiRenderPass(Device& device, Swapchain& swapchain);
VkCommandBuffer nativeCommandBuffer(Device& device, CommandList& commands);
VkImageView nativeImageView(Device& device, Texture& texture);
VkSampler nativeSampler(Device& device, Sampler& sampler);
}
