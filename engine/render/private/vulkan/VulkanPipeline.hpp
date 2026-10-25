#pragma once
#include <vulkan/vulkan.h>

namespace aeg::render {
struct ScenePipeline {
    VkDevice device{};
    VkRenderPass pass{};
    VkPipeline pipeline{};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    bool temporal{};
    ~ScenePipeline() {
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (pass) vkDestroyRenderPass(device, pass, nullptr);
    }
};
}
