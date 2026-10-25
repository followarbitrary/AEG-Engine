#pragma once
#include "vulkan/VulkanImage.hpp"
#include <array>
#include <cstdint>

namespace aeg::render {
struct ViewportTargets {
    VkDevice device{};
    VkDescriptorPool uiPool{}, postPool{};
    VkDescriptorSet descriptor{}, postDescriptor{};
    std::array<VkDescriptorSet, 2> temporalDescriptors{};
    VkFramebuffer sceneFrame{}, postFrame{};
    detail::Image color, depth, multisample, filtered;
    detail::Image motion;
    std::array<detail::Image, 2> historyColor, historyDepth;
    std::array<VkFramebuffer, 2> temporalFrames{};
    std::uint32_t outputHistory{};
    bool hasTemporalOutput{};
    std::uint32_t width{}, height{};
    ~ViewportTargets() {
        if (descriptor) vkFreeDescriptorSets(device, uiPool, 1, &descriptor);
        if (postPool) vkDestroyDescriptorPool(device, postPool, nullptr);
        if (postFrame) vkDestroyFramebuffer(device, postFrame, nullptr);
        if (sceneFrame) vkDestroyFramebuffer(device, sceneFrame, nullptr);
        for (auto frame : temporalFrames)
            if (frame) vkDestroyFramebuffer(device, frame, nullptr);
    }
    const detail::Image& output() const {
        if (filtered.handle) return filtered;
        return hasTemporalOutput ? historyColor[outputHistory] : color;
    }
};
}
