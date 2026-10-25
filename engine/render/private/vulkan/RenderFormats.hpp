#pragma once
#include <vulkan/vulkan.h>

namespace aeg::render {
inline constexpr VkFormat sceneFormat = VK_FORMAT_R8G8B8A8_UNORM;
inline constexpr VkFormat motionFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
inline constexpr VkFormat historyFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr VkFormat historyDepthFormat = VK_FORMAT_R32_SFLOAT;
}
