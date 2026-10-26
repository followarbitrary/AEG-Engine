#pragma once
#include <VulkanRenderer.hpp>
#include "vulkan/VulkanResources.hpp"
#include "vulkan/RenderFormats.hpp"
#include "vulkan/VulkanPipeline.hpp"
#include "scene/GpuModel.hpp"
#include "scene/SceneUniform.hpp"
#include "viewport/ViewportTargets.hpp"
#include "antialiasing/TemporalHistory.hpp"
#include <Utf8Path.hpp>
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <vector>

namespace aeg::render {
using assets::MaterialData;
using assets::ModelData;
using assets::Vertex;
using detail::check;

struct VulkanRenderer::Impl {
    GLFWwindow* window{};
    VkInstance instance{};
    VkDebugUtilsMessengerEXT messenger{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceProperties properties{};
    VkDevice device{};
    VkQueue graphicsQueue{}, presentQueue{};
    std::uint32_t graphicsFamily{}, presentFamily{};
    std::string gpuName;
    std::atomic_uint32_t validationErrors{0};
    bool validation{}, syncValidation{}, glfwBackend{}, imguiBackend{};
    detail::DeviceResources resources;
    VkCommandPool commandPool{};
    VkCommandBuffer command{};
    VkFence frameFence{};
    VkSemaphore acquired{};
    VkSwapchainKHR swapchain{};
    VkExtent2D extent{};
    VkFormat swapFormat{VK_FORMAT_UNDEFINED}, depthFormat{VK_FORMAT_UNDEFINED};
    std::uint32_t minimumImageCount{2};
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;
    std::vector<VkFramebuffer> swapFrames;
    std::vector<VkSemaphore> renderFinished;
    bool rebuildSwapchain{};
    VkRenderPass presentPass{}, postPass{}, temporalPass{};
    VkDescriptorPool uiPool{}, uniformPool{};
    VkDescriptorSetLayout sceneLayout{}, materialLayout{}, postSetLayout{}, temporalSetLayout{};
    VkDescriptorSet sceneDescriptor{};
    VkPipelineLayout pipelineLayout{}, postLayout{}, temporalLayout{};
    VkPipeline postPipeline{}, temporalPipeline{};
    VkSampler sampler{}, viewportSampler{}, pointSampler{};
    std::filesystem::path shaderDirectory;
    std::array<std::unique_ptr<ScenePipeline>, 4> scenePipelines;
    std::unique_ptr<ScenePipeline> temporalScene;
    AntialiasingSettings aa;
    std::uint32_t sampleMask{1}, maximumViewport{1};
    bool temporalSupported{};
    std::uint32_t maximumTemporalViewport{1};
    TemporalHistory temporalHistory;
    detail::Buffer uniformBuffer;
    std::unique_ptr<GpuModel> model;
    std::unique_ptr<ViewportTargets> viewport;
    bool sceneRendered{};

    ~Impl();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        VkDebugUtilsMessageTypeFlagsEXT,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* data, void* user);

    VkDebugUtilsMessengerCreateInfoEXT debugInfo();

    void createInstance(bool requestedValidation);

    void selectDevice();

    void createDevice();

    void createPresentPass();

    void createSwapchain();

    void destroySwapchain() noexcept;

    bool ensureSwapchain();

    void createScenePass(ScenePipeline& target);

    void createPostPass();

    void createDescriptors();

    VkPipeline createPipeline(VkRenderPass renderPass, VkPipelineLayout layout, VkSampleCountFlagBits samples,
                              bool scene, const char* vertexName, const char* fragmentName,
                              std::uint32_t colorAttachmentCount = 1);

    ScenePipeline& scenePipeline(std::uint32_t samples, bool temporal = false);

    void initialize(GLFWwindow* target, const std::filesystem::path& shaders, bool requestedValidation);

    void destroyViewport() noexcept;

    void resetTemporalHistory() noexcept;

    std::unique_ptr<ViewportTargets> createViewport(std::uint32_t width, std::uint32_t height,
                                                    const AntialiasingSettings& settings);

    void resizeViewport(std::uint32_t width, std::uint32_t height);

    void setAntialiasing(const AntialiasingSettings& requested);

    void uploadModel(const ModelData& source);

    static bool perspectiveProjection(const glm::mat4& matrix);

    SceneUniform sceneUniform(const RenderSettings& settings);

    void drawScene(const RenderSettings& settings);

    void drawTemporal();

    void pointFinalDescriptorAtTemporalOutput();

    void drawFxaa();

    void drawFrame(RenderSettings& settings, const std::function<void()>& ui);

    void captureViewport(const std::filesystem::path& path);

    void shutdown() noexcept;
};
}
