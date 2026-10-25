#include "VulkanRendererImpl.hpp"

namespace aeg::render {
void VulkanRenderer::Impl::createPresentPass() {
    VkAttachmentDescription attachment{};
    attachment.format = swapFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    check(vkCreateRenderPass(device, &info, nullptr, &presentPass), "vkCreateRenderPass(present)");
}

void VulkanRenderer::Impl::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    std::uint32_t count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (formats.empty()) throw std::runtime_error("Surface has no usable color format");
    auto chosen = formats.front();
    if (formats.size() == 1 && chosen.format == VK_FORMAT_UNDEFINED)
        chosen = {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (auto format : formats)
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = format;
            break;
        }
    if (swapFormat != VK_FORMAT_UNDEFINED && chosen.format != swapFormat)
        throw std::runtime_error("Surface color format changed; restart the editor for this display");
    swapFormat = chosen.format;
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    extent = caps.currentExtent;
    if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
        extent.width = std::clamp(static_cast<std::uint32_t>(std::max(width, 1)), caps.minImageExtent.width,
                                  caps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<std::uint32_t>(std::max(height, 1)), caps.minImageExtent.height,
                                   caps.maxImageExtent.height);
    }
    minimumImageCount = std::max(2u, caps.minImageCount);
    count = minimumImageCount + 1;
    if (caps.maxImageCount > 0) count = std::min(count, caps.maxImageCount);
    if (count < 2) throw std::runtime_error("ImGui requires at least two swapchain images");
    minimumImageCount = std::min(minimumImageCount, count);
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (auto candidate : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
        if (caps.supportedCompositeAlpha & candidate) {
            alpha = candidate;
            break;
        }
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = count;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const std::array families{graphicsFamily, presentFamily};
    info.imageSharingMode =
        graphicsFamily == presentFamily ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    if (info.imageSharingMode == VK_SHARING_MODE_CONCURRENT) {
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = families.data();
    }
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "vkCreateSwapchainKHR");
    check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "vkGetSwapchainImagesKHR");
    swapImages.resize(count);
    check(vkGetSwapchainImagesKHR(device, swapchain, &count, swapImages.data()), "vkGetSwapchainImagesKHR");
    if (!presentPass) createPresentPass();
    for (auto image : swapImages) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = swapFormat;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView handle{};
        check(vkCreateImageView(device, &view, nullptr, &handle), "vkCreateImageView(swapchain)");
        swapViews.push_back(handle);
        VkFramebufferCreateInfo frame{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        frame.renderPass = presentPass;
        frame.attachmentCount = 1;
        frame.pAttachments = &handle;
        frame.width = extent.width;
        frame.height = extent.height;
        frame.layers = 1;
        VkFramebuffer buffer{};
        check(vkCreateFramebuffer(device, &frame, nullptr, &buffer), "vkCreateFramebuffer(present)");
        swapFrames.push_back(buffer);
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore finished{};
        check(vkCreateSemaphore(device, &semaphore, nullptr, &finished), "vkCreateSemaphore(present)");
        renderFinished.push_back(finished);
    }
}

void VulkanRenderer::Impl::destroySwapchain() noexcept {
    for (auto frame : swapFrames) vkDestroyFramebuffer(device, frame, nullptr);
    for (auto view : swapViews) vkDestroyImageView(device, view, nullptr);
    for (auto semaphore : renderFinished) vkDestroySemaphore(device, semaphore, nullptr);
    swapFrames.clear();
    swapViews.clear();
    renderFinished.clear();
    swapImages.clear();
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

bool VulkanRenderer::Impl::ensureSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (!width || !height) {
        glfwWaitEventsTimeout(0.05);
        return false;
    }
    if (rebuildSwapchain || static_cast<std::uint32_t>(width) != extent.width ||
        static_cast<std::uint32_t>(height) != extent.height) {
        check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(resize)");
        destroySwapchain();
        createSwapchain();
        if (imguiBackend) ImGui_ImplVulkan_SetMinImageCount(minimumImageCount);
        rebuildSwapchain = false;
    }
    return true;
}
}
