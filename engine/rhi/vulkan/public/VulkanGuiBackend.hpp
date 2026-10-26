#pragma once
#include <GuiRenderer.hpp>
#include <RhiSwapchain.hpp>
namespace aeg::rhi::vulkan {
[[nodiscard]] std::unique_ptr<gui::GuiRenderer> createVulkanGuiRenderer(Device& device, Swapchain& swapchain);
}
