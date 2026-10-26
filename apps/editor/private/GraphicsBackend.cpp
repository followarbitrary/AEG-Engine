#include "GraphicsBackend.hpp"
#include <VulkanBackend.hpp>
#include <VulkanGuiBackend.hpp>

namespace aeg::app {
editor::EditorBackend createGraphicsBackend(const rhi::NativeWindow& window, bool validation) {
    editor::EditorBackend backend;
    backend.device = rhi::vulkan::createVulkanDevice({window, validation});
    backend.swapchain = rhi::vulkan::createVulkanSwapchain(*backend.device);
    backend.gui = rhi::vulkan::createVulkanGuiRenderer(*backend.device, *backend.swapchain);
    return backend;
}
}
