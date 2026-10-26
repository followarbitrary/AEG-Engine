#include "VulkanRendererImpl.hpp"

namespace aeg::render {
VulkanRenderer::Impl::~Impl() { shutdown(); }

void VulkanRenderer::Impl::initialize(GLFWwindow* target, const std::filesystem::path& shaders,
                                      bool requestedValidation) {
    window = target;
    shaderDirectory = shaders;
    createInstance(requestedValidation);
    selectDevice();
    createDevice();
    createSwapchain();
    createDescriptors();
    scenePipeline(1);
    createPostPass();
    postPipeline =
        createPipeline(postPass, postLayout, VK_SAMPLE_COUNT_1_BIT, false, "fullscreen.vert.spv", "fxaa.frag.spv");
    if (temporalSupported)
        temporalPipeline = createPipeline(temporalPass, temporalLayout, VK_SAMPLE_COUNT_1_BIT, false,
                                          "fullscreen.vert.spv", "taa.frag.spv", 2);
    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) throw std::runtime_error("ImGui GLFW initialization failed");
    glfwBackend = true;
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_2;
    info.Instance = instance;
    info.PhysicalDevice = physical;
    info.Device = device;
    info.QueueFamily = graphicsFamily;
    info.Queue = graphicsQueue;
    info.DescriptorPool = uiPool;
    info.RenderPass = presentPass;
    info.MinImageCount = minimumImageCount;
    info.ImageCount = static_cast<std::uint32_t>(swapImages.size());
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = [](VkResult result) { check(result, "ImGui Vulkan backend"); };
    try {
        if (!ImGui_ImplVulkan_Init(&info)) throw std::runtime_error("ImGui Vulkan initialization failed");
        imguiBackend = true;
    } catch (...) {
        // The backend registers its state before allocating device objects.
        // An allocation failure still leaves that partial state to destroy.
        imguiBackend = ImGui::GetIO().BackendRendererUserData != nullptr;
        throw;
    }
    if (!ImGui_ImplVulkan_CreateFontsTexture()) throw std::runtime_error("ImGui font upload failed");
    resizeViewport(960, 640);
}

void VulkanRenderer::Impl::drawFrame(RenderSettings& settings, const std::function<void()>& ui) {
    check(vkWaitForFences(device, 1, &frameFence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    if (!ensureSwapchain()) return;
    std::uint32_t index = 0;
    const auto result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        rebuildSwapchain = true;
        return;
    }
    if (result != VK_SUBOPTIMAL_KHR) check(result, "vkAcquireNextImageKHR");
    if (result == VK_SUBOPTIMAL_KHR) rebuildSwapchain = true;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ui();
    ImGui::Render();
    check(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer(frame)");
    drawScene(settings);
    // sceneUniform may reset history and its ping-pong index for new shading.
    pointFinalDescriptorAtTemporalOutput();
    drawTemporal();
    drawFxaa();
    VkClearValue clear{};
    clear.color = {{0.02f, 0.025f, 0.035f, 1}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = presentPass;
    pass.framebuffer = swapFrames[index];
    pass.renderArea = {{0, 0}, extent};
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
    vkCmdEndRenderPass(command);
    check(vkEndCommandBuffer(command), "vkEndCommandBuffer(frame)");
    // A present wait semaphore belongs to the acquired swapchain image, not
    // to the frame fence. Acquisition guarantees its previous present wait completed.
    const auto finished = renderFinished[index];
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &acquired;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &finished;
    // Only reset once a submission will occur; out-of-date/minimized paths keep a signaled fence.
    check(vkResetFences(device, 1, &frameFence), "vkResetFences");
    check(vkQueueSubmit(graphicsQueue, 1, &submit, frameFence), "vkQueueSubmit(frame)");
    if (aa.taa) {
        temporalHistory.readIndex = 1u - temporalHistory.readIndex;
        viewport->outputHistory = temporalHistory.readIndex;
        viewport->hasTemporalOutput = true;
        temporalHistory.previous = settings;
        temporalHistory.valid = true;
        if (temporalHistory.frames != std::numeric_limits<std::uint32_t>::max()) ++temporalHistory.frames;
        temporalHistory.jitterIndex = (temporalHistory.jitterIndex + 1) % 8;
    }
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &finished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &index;
    const auto presented = vkQueuePresentKHR(presentQueue, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
        rebuildSwapchain = true;
    else
        check(presented, "vkQueuePresentKHR");
}

void VulkanRenderer::Impl::shutdown() noexcept {
    if (device) vkDeviceWaitIdle(device);
    model.reset();
    destroyViewport();
    if (imguiBackend) {
        ImGui_ImplVulkan_Shutdown();
        imguiBackend = false;
    }
    if (glfwBackend) {
        ImGui_ImplGlfw_Shutdown();
        glfwBackend = false;
    }
    if (device) {
        uniformBuffer.reset();
        destroySwapchain();
        for (auto& scene : scenePipelines) scene.reset();
        temporalScene.reset();
        if (temporalPipeline) vkDestroyPipeline(device, temporalPipeline, nullptr);
        if (temporalLayout) vkDestroyPipelineLayout(device, temporalLayout, nullptr);
        if (postPipeline) vkDestroyPipeline(device, postPipeline, nullptr);
        if (postLayout) vkDestroyPipelineLayout(device, postLayout, nullptr);
        if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (uniformPool) vkDestroyDescriptorPool(device, uniformPool, nullptr);
        if (uiPool) vkDestroyDescriptorPool(device, uiPool, nullptr);
        if (sceneLayout) vkDestroyDescriptorSetLayout(device, sceneLayout, nullptr);
        if (materialLayout) vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
        if (postSetLayout) vkDestroyDescriptorSetLayout(device, postSetLayout, nullptr);
        if (temporalSetLayout) vkDestroyDescriptorSetLayout(device, temporalSetLayout, nullptr);
        if (sampler) vkDestroySampler(device, sampler, nullptr);
        if (viewportSampler) vkDestroySampler(device, viewportSampler, nullptr);
        if (pointSampler) vkDestroySampler(device, pointSampler, nullptr);
        if (temporalPass) vkDestroyRenderPass(device, temporalPass, nullptr);
        if (postPass) vkDestroyRenderPass(device, postPass, nullptr);
        if (presentPass) vkDestroyRenderPass(device, presentPass, nullptr);
        if (frameFence) vkDestroyFence(device, frameFence, nullptr);
        if (acquired) vkDestroySemaphore(device, acquired, nullptr);
        if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (messenger) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance, messenger, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
}

VulkanRenderer::VulkanRenderer(GLFWwindow* window, const std::filesystem::path& shaders, bool validation)
    : impl_(std::make_unique<Impl>()) {
    impl_->initialize(window, shaders, validation);
}

VulkanRenderer::~VulkanRenderer() = default;

void VulkanRenderer::uploadModel(const ModelData& model) { impl_->uploadModel(model); }

void VulkanRenderer::setAntialiasing(const AntialiasingSettings& settings) { impl_->setAntialiasing(settings); }
AntialiasingSettings VulkanRenderer::antialiasing() const { return impl_->aa; }
std::uint32_t VulkanRenderer::supportedMsaaSamples() const { return impl_->sampleMask; }
bool VulkanRenderer::supportsTemporalAA() const { return impl_->temporalSupported; }
void VulkanRenderer::resetTemporalHistory() { impl_->resetTemporalHistory(); }
std::uint32_t VulkanRenderer::temporalFrameCount() const { return impl_->temporalHistory.frames; }

void VulkanRenderer::resizeViewport(std::uint32_t width, std::uint32_t height) { impl_->resizeViewport(width, height); }

ImTextureID VulkanRenderer::viewportTexture() const {
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(impl_->viewport->descriptor));
}

void VulkanRenderer::drawFrame(RenderSettings& settings, const std::function<void()>& ui) {
    impl_->drawFrame(settings, ui);
}

void VulkanRenderer::captureViewport(const std::filesystem::path& output) { impl_->captureViewport(output); }
void VulkanRenderer::waitIdle() { check(vkDeviceWaitIdle(impl_->device), "vkDeviceWaitIdle"); }
const std::string& VulkanRenderer::deviceName() const { return impl_->gpuName; }
std::uint32_t VulkanRenderer::validationErrorCount() const { return impl_->validationErrors.load(); }

}
