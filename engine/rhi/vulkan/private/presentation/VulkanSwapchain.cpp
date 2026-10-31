#include "VulkanBackendInternal.hpp"

namespace aeg::rhi::vulkan::detail {
VulkanSwapchain::VulkanSwapchain(VulkanDevice& device) : owner(&device), commands(device) {
    try {
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = owner->pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(owner->handle, &allocate, &commands.handle), "vkAllocateCommandBuffers(frame)");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(owner->handle, &fenceInfo, nullptr, &fence), "vkCreateFence(frame)");
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(owner->handle, &semaphore, nullptr, &acquired), "vkCreateSemaphore(acquire)");
        create();
    } catch (...) {
        cleanup();
        throw;
    }
}
VulkanSwapchain::~VulkanSwapchain() { cleanup(); }
void VulkanSwapchain::destroyImages() noexcept {
    frames.clear();
    loadFrames.clear();
    images.clear();
    imageStates.clear();
    for (auto view : views) vkDestroyImageView(owner->handle, view, nullptr);
    for (auto semaphore : finished) vkDestroySemaphore(owner->handle, semaphore, nullptr);
    views.clear();
    finished.clear();
    if (handle) {
        vkDestroySwapchainKHR(owner->handle, handle, nullptr);
        handle = VK_NULL_HANDLE;
    }
}
void VulkanSwapchain::cleanup() noexcept {
    vkDeviceWaitIdle(owner->handle);
    destroyImages();
    pass.reset();
    loadPass.reset();
    if (acquired) {
        vkDestroySemaphore(owner->handle, acquired, nullptr);
        acquired = VK_NULL_HANDLE;
    }
    if (fence) {
        vkDestroyFence(owner->handle, fence, nullptr);
        fence = VK_NULL_HANDLE;
    }
    if (commands.handle) {
        vkFreeCommandBuffers(owner->handle, owner->pool, 1, &commands.handle);
        commands.handle = VK_NULL_HANDLE;
    }
}
void VulkanSwapchain::create() {
    VkSurfaceCapabilitiesKHR caps{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(owner->physical, owner->surface, &caps),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    std::uint32_t count{};
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(owner->physical, owner->surface, &count, nullptr),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(owner->physical, owner->surface, &count, formats.data()),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (formats.empty()) throw std::runtime_error("Surface has no usable color format");
    auto chosen = formats.front();
    if (formats.size() == 1 && chosen.format == VK_FORMAT_UNDEFINED)
        chosen = {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (auto candidate : formats)
        if (candidate.format == VK_FORMAT_B8G8R8A8_SRGB && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = candidate;
            break;
        }
    if (format != VK_FORMAT_UNDEFINED && chosen.format != format)
        throw std::runtime_error("Surface color format changed; restart the editor for this display");
    format = chosen.format;
    auto requested = owner->creationDesc.window.framebufferExtent();
    if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
        size = {caps.currentExtent.width, caps.currentExtent.height};
    else
        size = {std::clamp(std::max(requested.width, 1u), caps.minImageExtent.width, caps.maxImageExtent.width),
                std::clamp(std::max(requested.height, 1u), caps.minImageExtent.height, caps.maxImageExtent.height)};
    if (!size.width || !size.height)
        throw std::runtime_error("Cannot create swapchain for a zero-sized native surface");
    minimum = std::max(2u, caps.minImageCount);
    count = minimum + 1;
    if (caps.maxImageCount) count = std::min(count, caps.maxImageCount);
    if (count < 2) throw std::runtime_error("Presentation requires at least two swapchain images");
    minimum = std::min(minimum, count);
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (auto candidate : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
        if (caps.supportedCompositeAlpha & candidate) {
            alpha = candidate;
            break;
        }
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = owner->surface;
    info.minImageCount = count;
    info.imageFormat = format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = {size.width, size.height};
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const std::array families{owner->graphicsFamily, owner->presentFamily};
    info.imageSharingMode = families[0] == families[1] ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    if (info.imageSharingMode == VK_SHARING_MODE_CONCURRENT) {
        info.queueFamilyIndexCount = static_cast<std::uint32_t>(families.size());
        info.pQueueFamilyIndices = families.data();
    }
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(owner->handle, &info, nullptr, &handle), "vkCreateSwapchainKHR");
    check(vkGetSwapchainImagesKHR(owner->handle, handle, &count, nullptr), "vkGetSwapchainImagesKHR");
    std::vector<VkImage> nativeImages(count);
    check(vkGetSwapchainImagesKHR(owner->handle, handle, &count, nativeImages.data()), "vkGetSwapchainImagesKHR");
    if (!pass) {
        RenderPassDesc desc;
        desc.attachments.push_back({genericFormat(format), 1, LoadOp::Clear, StoreOp::Store,
                                    ResourceState::ColorAttachment, ResourceState::ColorAttachment});
        desc.colors.push_back(0);
        pass = owner->createRenderPass(desc);
    }
    if (!loadPass) {
        RenderPassDesc desc;
        desc.attachments.push_back({genericFormat(format), 1, LoadOp::Load, StoreOp::Store,
                                    ResourceState::ColorAttachment, ResourceState::ColorAttachment});
        desc.colors.push_back(0);
        loadPass = owner->createRenderPass(desc);
    }
    auto& nativePass = owned<VulkanRenderPass>(*owner, *pass);
    auto& nativeLoadPass = owned<VulkanRenderPass>(*owner, *loadPass);
    // Reserve before creating native objects so a vector allocation cannot strand a native handle.
    views.reserve(nativeImages.size());
    frames.reserve(nativeImages.size());
    loadFrames.reserve(nativeImages.size());
    finished.reserve(nativeImages.size());
    images.reserve(nativeImages.size());
    imageStates.reserve(nativeImages.size());
    for (auto image : nativeImages) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView nativeView{};
        check(vkCreateImageView(owner->handle, &view, nullptr, &nativeView), "vkCreateImageView(swapchain)");
        views.push_back(nativeView);
        auto texture = std::make_unique<VulkanTexture>(*owner,
                                                       TextureDesc{{size.width, size.height}, genericFormat(format),
                                                                   TextureUsage::ColorAttachment});
        texture->handle = image;
        texture->view = nativeView;
        texture->nonOwning = true;
        images.push_back(std::move(texture));
        imageStates.push_back(ResourceState::Undefined);
        auto frame = std::make_unique<VulkanFramebuffer>(*owner);
        frame->pass = &nativePass;
        frame->size = size;
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = nativePass.handle;
        framebuffer.attachmentCount = 1;
        framebuffer.pAttachments = &nativeView;
        framebuffer.width = size.width;
        framebuffer.height = size.height;
        framebuffer.layers = 1;
        check(vkCreateFramebuffer(owner->handle, &framebuffer, nullptr, &frame->handle),
              "vkCreateFramebuffer(swapchain)");
        frames.push_back(std::move(frame));
        auto loadFrame = std::make_unique<VulkanFramebuffer>(*owner);
        loadFrame->pass = &nativeLoadPass;
        loadFrame->size = size;
        VkFramebufferCreateInfo loadFramebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        loadFramebuffer.renderPass = nativeLoadPass.handle;
        loadFramebuffer.attachmentCount = 1;
        loadFramebuffer.pAttachments = &nativeView;
        loadFramebuffer.width = size.width;
        loadFramebuffer.height = size.height;
        loadFramebuffer.layers = 1;
        check(vkCreateFramebuffer(owner->handle, &loadFramebuffer, nullptr, &loadFrame->handle),
              "vkCreateFramebuffer(swapchain-load)");
        loadFrames.push_back(std::move(loadFrame));
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore signal{};
        check(vkCreateSemaphore(owner->handle, &semaphore, nullptr, &signal), "vkCreateSemaphore(present)");
        finished.push_back(signal);
    }
}
bool VulkanSwapchain::beginFrame() {
    if (acquiredFrame || submitted) throw std::logic_error("Previous swapchain frame was not presented");
    check(vkWaitForFences(owner->handle, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(frame)");
    const auto requested = owner->creationDesc.window.framebufferExtent();
    if (!requested.width || !requested.height) return false;
    if (rebuild || requested != size) {
        owner->waitIdle();
        destroyImages();
        create();
        rebuild = false;
    }
    const auto result = vkAcquireNextImageKHR(owner->handle, handle, UINT64_MAX, acquired, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        rebuild = true;
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR)
        rebuild = true;
    else
        check(result, "vkAcquireNextImageKHR");
    check(vkResetCommandBuffer(commands.handle, 0), "vkResetCommandBuffer(frame)");
    commands.inPass = false;
    commands.indexBound = false;
    commands.computePipeline = nullptr;
    commands.computeBoundLayout = nullptr;
    commands.computeSets.clear();
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commands.handle, &begin), "vkBeginCommandBuffer(frame)");
    acquiredFrame = true;
    return true;
}
CommandList& VulkanSwapchain::commandList() {
    if (!acquiredFrame) throw std::logic_error("No recording swapchain frame");
    return commands;
}
Framebuffer& VulkanSwapchain::framebuffer() {
    if (!acquiredFrame) throw std::logic_error("No acquired swapchain framebuffer");
    return *frames.at(imageIndex);
}
Framebuffer& VulkanSwapchain::loadFramebuffer() {
    if (!acquiredFrame) throw std::logic_error("No acquired swapchain framebuffer");
    return *loadFrames.at(imageIndex);
}
Texture& VulkanSwapchain::image() {
    if (!acquiredFrame) throw std::logic_error("No acquired swapchain image");
    return *images.at(imageIndex);
}
ResourceState VulkanSwapchain::imageState() const {
    if (!acquiredFrame) throw std::logic_error("No acquired swapchain image");
    return imageStates.at(imageIndex);
}
void VulkanSwapchain::submit() {
    if (!acquiredFrame || submitted || commands.inPass)
        throw std::logic_error("Swapchain frame is not ready to submit");
    check(vkEndCommandBuffer(commands.handle), "vkEndCommandBuffer(frame)");
    // Reset only after successful acquisition and recording. A skipped frame keeps its signaled fence.
    check(vkResetFences(owner->handle, 1, &fence), "vkResetFences(frame)");
    // The graph emits the acquire-image layout transition before beginning the
    // first render pass.  Wait at ALL_COMMANDS so the acquire semaphore also
    // orders that explicit barrier (not just color attachment writes).
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &acquired;
    info.pWaitDstStageMask = &waitStage;
    info.commandBufferCount = 1;
    info.pCommandBuffers = &commands.handle;
    info.signalSemaphoreCount = 1;
    info.pSignalSemaphores = &finished.at(imageIndex);
    check(vkQueueSubmit(owner->graphicsQueue, 1, &info, fence), "vkQueueSubmit(frame)");
    acquiredFrame = false;
    submitted = true;
}
void VulkanSwapchain::present() {
    if (!submitted) throw std::logic_error("No submitted swapchain frame to present");
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &finished.at(imageIndex);
    info.swapchainCount = 1;
    info.pSwapchains = &handle;
    info.pImageIndices = &imageIndex;
    const auto result = vkQueuePresentKHR(owner->presentQueue, &info);
    submitted = false;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        rebuild = true;
        if (result == VK_SUBOPTIMAL_KHR) imageStates.at(imageIndex) = ResourceState::Present;
    } else {
        check(result, "vkQueuePresentKHR");
        imageStates.at(imageIndex) = ResourceState::Present;
    }
}
VkRenderPass nativeGuiRenderPass(Device& device, Swapchain& swapchain) {
    auto& native = nativeDevice(device);
    auto& swap = owned<VulkanSwapchain>(native, swapchain);
    return owned<VulkanRenderPass>(native, *swap.loadPass).handle;
}
}
namespace aeg::rhi::vulkan {
std::unique_ptr<Swapchain> createVulkanSwapchain(Device& device) {
    return std::make_unique<detail::VulkanSwapchain>(detail::nativeDevice(device));
}
}
