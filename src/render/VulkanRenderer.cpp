#include <aeg/VulkanRenderer.hpp>
#include "VulkanResources.hpp"
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

namespace aeg {
using detail::check;

namespace {
constexpr VkFormat sceneFormat = VK_FORMAT_R8G8B8A8_UNORM;

struct alignas(16) SceneUniform {
    glm::mat4 viewProjection{1};
    glm::mat4 model{1};
    glm::vec4 lightDirection{0, -1, 0, 0};
    glm::vec4 parameters{0.28f, 0, 0, 0};
};

static_assert(sizeof(SceneUniform) == 160);

struct MaterialPush {
    glm::vec4 color{1};
    float alphaCutoff{0.5f};
    std::uint32_t alphaMasked{};
    glm::vec2 reserved{};
};

static_assert(sizeof(MaterialPush) == 32);
static_assert(offsetof(MaterialPush, alphaCutoff) == 16 && offsetof(MaterialPush, alphaMasked) == 20);

struct GpuMesh {
    detail::Buffer vertices, indices;
    std::uint32_t indexCount{}, material{};
};

struct GpuMaterial {
    detail::Image image;
    VkDescriptorSet descriptor{};
    MaterialPush parameters;
};

struct GpuModel {
    VkDevice device{};
    VkDescriptorPool pool{};
    std::vector<GpuMesh> meshes;
    std::vector<GpuMaterial> materials;
    ~GpuModel() {
        if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
    }
};

struct ScenePipeline {
    VkDevice device{};
    VkRenderPass pass{};
    VkPipeline pipeline{};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
    ~ScenePipeline() {
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (pass) vkDestroyRenderPass(device, pass, nullptr);
    }
};

// A complete candidate owns all its handles, including on partial allocation
// failure. Framebuffers/descriptors are released before the image views.
struct ViewportTargets {
    VkDevice device{};
    VkDescriptorPool uiPool{}, postPool{};
    VkDescriptorSet descriptor{}, postDescriptor{};
    VkFramebuffer sceneFrame{}, postFrame{};
    detail::Image color, depth, multisample, filtered;
    std::uint32_t width{}, height{};
    ~ViewportTargets() {
        if (descriptor) vkFreeDescriptorSets(device, uiPool, 1, &descriptor);
        if (postPool) vkDestroyDescriptorPool(device, postPool, nullptr);
        if (postFrame) vkDestroyFramebuffer(device, postFrame, nullptr);
        if (sceneFrame) vkDestroyFramebuffer(device, sceneFrame, nullptr);
    }
    const detail::Image& output() const { return filtered.handle ? filtered : color; }
};

std::string pathText(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::vector<std::uint32_t> readShader(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open shader: " + pathText(path));
    const auto length = input.tellg();
    if (length <= 0 || static_cast<std::size_t>(length) % 4 != 0) throw std::runtime_error("Invalid SPIR-V shader");
    std::vector<std::uint32_t> result(static_cast<std::size_t>(length) / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(result.data()), length);
    if (!input) throw std::runtime_error("Cannot read SPIR-V shader");
    return result;
}
}

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
    VkRenderPass presentPass{}, postPass{};
    VkDescriptorPool uiPool{}, uniformPool{};
    VkDescriptorSetLayout sceneLayout{}, materialLayout{};
    VkDescriptorSet sceneDescriptor{};
    VkPipelineLayout pipelineLayout{}, postLayout{};
    VkPipeline postPipeline{};
    VkSampler sampler{}, viewportSampler{};
    std::filesystem::path shaderDirectory;
    std::array<std::unique_ptr<ScenePipeline>, 4> scenePipelines;
    AntialiasingSettings aa;
    std::uint32_t sampleMask{1}, maximumViewport{1};
    detail::Buffer uniformBuffer;
    std::unique_ptr<GpuModel> model;
    std::unique_ptr<ViewportTargets> viewport;
    bool sceneRendered{};

    ~Impl() { shutdown(); }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        VkDebugUtilsMessageTypeFlagsEXT,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
        auto& self = *static_cast<Impl*>(user);
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            ++self.validationErrors;
            std::cerr << "[Vulkan error] " << data->pMessage << '\n';
        } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            std::cerr << "[Vulkan warning] " << data->pMessage << '\n';
        }
        return VK_FALSE;
    }

    VkDebugUtilsMessengerCreateInfoEXT debugInfo() {
        VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = debugCallback;
        info.pUserData = this;
        return info;
    }

    void createInstance(bool requestedValidation) {
        std::uint32_t loaderVersion = VK_API_VERSION_1_0;
        auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
        if (enumerateVersion) check(enumerateVersion(&loaderVersion), "vkEnumerateInstanceVersion");
        if (loaderVersion < VK_API_VERSION_1_2) throw std::runtime_error("Vulkan 1.2 or newer is required");
        std::uint32_t extensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (!glfwExtensions) throw std::runtime_error("GLFW could not find Vulkan surface extensions");
        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + extensionCount);
        std::uint32_t layerCount = 0;
        check(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "vkEnumerateInstanceLayerProperties");
        std::vector<VkLayerProperties> layers(layerCount);
        check(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()), "vkEnumerateInstanceLayerProperties");
        const char* validationLayer = "VK_LAYER_KHRONOS_validation";
        validation = requestedValidation && std::ranges::any_of(layers, [&](const auto& layer) {
                         return std::string_view(layer.layerName) == validationLayer;
                     });
        if (requestedValidation && !validation) std::cerr << "Validation layer unavailable; continuing without it.\n";
        if (validation) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            std::uint32_t count = 0;
            check(vkEnumerateInstanceExtensionProperties(validationLayer, &count, nullptr),
                  "vkEnumerateInstanceExtensionProperties");
            std::vector<VkExtensionProperties> available(count);
            check(vkEnumerateInstanceExtensionProperties(validationLayer, &count, available.data()),
                  "vkEnumerateInstanceExtensionProperties");
            syncValidation = std::ranges::any_of(available, [](const auto& entry) {
                return std::string_view(entry.extensionName) == VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
            });
            if (syncValidation) extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        }
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "AEG Engine";
        app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app.pEngineName = "AEG";
        app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app.apiVersion = VK_API_VERSION_1_2;
        auto debug = debugInfo();
        const VkValidationFeatureEnableEXT enabled = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
        features.enabledValidationFeatureCount = 1;
        features.pEnabledValidationFeatures = &enabled;
        if (syncValidation) debug.pNext = &features;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        if (validation) {
            info.enabledLayerCount = 1;
            info.ppEnabledLayerNames = &validationLayer;
            info.pNext = &debug;
        }
        check(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");
        if (validation) {
            auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if (!create) throw std::runtime_error("Vulkan debug messenger entry point missing");
            debug.pNext = nullptr;
            check(create(instance, &debug, nullptr, &messenger), "vkCreateDebugUtilsMessengerEXT");
        }
        check(glfwCreateWindowSurface(instance, window, nullptr, &surface), "glfwCreateWindowSurface");
    }

    void selectDevice() {
        std::uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        std::vector<VkPhysicalDevice> candidates(count);
        check(vkEnumeratePhysicalDevices(instance, &count, candidates.data()), "vkEnumeratePhysicalDevices");
        int best = -1;
        for (auto candidate : candidates) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(candidate, &props);
            if (props.apiVersion < VK_API_VERSION_1_2) continue;
            std::uint32_t extensionCount = 0;
            check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr),
                  "vkEnumerateDeviceExtensionProperties");
            std::vector<VkExtensionProperties> extensions(extensionCount);
            check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data()),
                  "vkEnumerateDeviceExtensionProperties");
            if (!std::ranges::any_of(extensions, [](const auto& entry) {
                    return std::string_view(entry.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
                }))
                continue;
            std::uint32_t families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
            std::vector<VkQueueFamilyProperties> queues(families);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, queues.data());
            std::optional<std::uint32_t> graphics, present;
            for (std::uint32_t i = 0; i < families; ++i) {
                VkBool32 supportsPresent = VK_FALSE;
                check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &supportsPresent),
                      "vkGetPhysicalDeviceSurfaceSupportKHR");
                if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) graphics = i;
                if (queues[i].queueCount && supportsPresent) present = i;
                if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && supportsPresent) {
                    graphics = i;
                    present = i;
                    break;
                }
            }
            if (!graphics || !present) continue;
            std::uint32_t formats = 0, modes = 0;
            check(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formats, nullptr),
                  "vkGetPhysicalDeviceSurfaceFormatsKHR");
            check(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &modes, nullptr),
                  "vkGetPhysicalDeviceSurfacePresentModesKHR");
            if (!formats || !modes) continue;
            int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 10;
            if (score > best) {
                best = score;
                physical = candidate;
                graphicsFamily = *graphics;
                presentFamily = *present;
                properties = props;
            }
        }
        if (!physical) throw std::runtime_error("No Vulkan 1.2 GPU with graphics and presentation support found");
        gpuName = properties.deviceName;
        std::cout << "Vulkan GPU: " << gpuName << " | validation " << (validation ? "on" : "off")
                  << (syncValidation ? " (synchronization enabled)" : "") << '\n';
        for (const auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
            VkFormatProperties supported{};
            vkGetPhysicalDeviceFormatProperties(physical, format, &supported);
            if (supported.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                depthFormat = format;
                break;
            }
        }
        if (depthFormat == VK_FORMAT_UNDEFINED) throw std::runtime_error("No compatible depth attachment format");
        const auto imageSupport = [&](VkFormat format, VkImageUsageFlags usage) {
            VkImageFormatProperties info{};
            check(vkGetPhysicalDeviceImageFormatProperties(physical, format, VK_IMAGE_TYPE_2D,
                      VK_IMAGE_TILING_OPTIMAL, usage, 0, &info), "vkGetPhysicalDeviceImageFormatProperties");
            return info;
        };
        const auto color = imageSupport(sceneFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        const auto single = imageSupport(sceneFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        const auto depth = imageSupport(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        sampleMask = properties.limits.framebufferColorSampleCounts &
            properties.limits.framebufferDepthSampleCounts & color.sampleCounts & depth.sampleCounts & 15u;
        if (!(sampleMask & 1u) || !(single.sampleCounts & VK_SAMPLE_COUNT_1_BIT))
            throw std::runtime_error("Scene attachments do not support single sampling");
        VkFormatProperties format{};
        vkGetPhysicalDeviceFormatProperties(physical, sceneFormat, &format);
        if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
            throw std::runtime_error("Scene format does not support linear sampling");
        maximumViewport = std::min({properties.limits.maxImageDimension2D,
            properties.limits.maxFramebufferWidth, properties.limits.maxFramebufferHeight,
            color.maxExtent.width, color.maxExtent.height, single.maxExtent.width, single.maxExtent.height,
            depth.maxExtent.width, depth.maxExtent.height, 8192u});
        std::cout << "Supported MSAA mask: " << sampleMask << '\n';
    }

    void createDevice() {
        const std::set<std::uint32_t> families{graphicsFamily, presentFamily};
        const float priority = 1;
        std::vector<VkDeviceQueueCreateInfo> queues;
        for (auto family : families) {
            VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queue.queueFamilyIndex = family;
            queue.queueCount = 1;
            queue.pQueuePriorities = &priority;
            queues.push_back(queue);
        }
        const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.queueCreateInfoCount = static_cast<std::uint32_t>(queues.size());
        info.pQueueCreateInfos = queues.data();
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = &extension;
        check(vkCreateDevice(physical, &info, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
        vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = graphicsFamily;
        check(vkCreateCommandPool(device, &pool, nullptr, &commandPool), "vkCreateCommandPool");
        resources = {physical, device, graphicsQueue, commandPool};
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &allocation, &command), "vkAllocateCommandBuffers(frame)");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device, &fence, nullptr, &frameFence), "vkCreateFence");
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device, &semaphore, nullptr, &acquired), "vkCreateSemaphore(acquire)");
    }

    void createPresentPass() {
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

    void createSwapchain() {
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

    void destroySwapchain() noexcept {
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

    bool ensureSwapchain() {
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

    void createScenePass(ScenePipeline& target) {
        const bool multisampled = target.samples != VK_SAMPLE_COUNT_1_BIT;
        std::array<VkAttachmentDescription, 3> attachments{};
        attachments[0].format = sceneFormat;
        attachments[0].samples = target.samples;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = multisampled ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                 : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[1].format = depthFormat;
        attachments[1].samples = target.samples;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[2] = attachments[0];
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
            resolve{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        subpass.pDepthStencilAttachment = &depth;
        subpass.pResolveAttachments = multisampled ? &resolve : nullptr;
        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        // The sampled scene is drawn at a different offset/scale in ImGui.
        // Framebuffer-local dependencies would not synchronize those texels.
        dependencies[0].dependencyFlags = 0;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = 0;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = multisampled ? 3u : 2u;
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2;
        info.pDependencies = dependencies.data();
        check(vkCreateRenderPass(device, &info, nullptr, &target.pass), "vkCreateRenderPass(scene)");
    }

    void createPostPass() {
        VkAttachmentDescription color{};
        color.format = sceneFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference output{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &output;
        std::array<VkSubpassDependency, 2> dependencies{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        // Global scopes: FXAA reads neighboring texels and ImGui scales the result.
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = 1;
        info.pAttachments = &color;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
        info.pDependencies = dependencies.data();
        check(vkCreateRenderPass(device, &info, nullptr, &postPass), "vkCreateRenderPass(FXAA)");
    }

    void createDescriptors() {
        VkDescriptorSetLayoutBinding uniform{};
        uniform.binding = 0;
        uniform.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniform.descriptorCount = 1;
        uniform.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = 1;
        layout.pBindings = &uniform;
        check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &sceneLayout),
              "vkCreateDescriptorSetLayout(scene)");
        VkDescriptorSetLayoutBinding texture{};
        texture.binding = 0;
        texture.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texture.descriptorCount = 1;
        texture.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        layout.pBindings = &texture;
        check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &materialLayout),
              "vkCreateDescriptorSetLayout(material)");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &pool, nullptr, &uniformPool), "vkCreateDescriptorPool(uniform)");
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = uniformPool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &sceneLayout;
        check(vkAllocateDescriptorSets(device, &allocate, &sceneDescriptor), "vkAllocateDescriptorSets(scene)");
        uniformBuffer =
            resources.buffer(sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
        VkDescriptorBufferInfo buffer{uniformBuffer.handle, 0, sizeof(SceneUniform)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = sceneDescriptor;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &buffer;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128};
        pool.maxSets = 128;
        pool.pPoolSizes = &size;
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        check(vkCreateDescriptorPool(device, &pool, nullptr, &uiPool), "vkCreateDescriptorPool(ImGui)");
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.minLod = 0;
        info.maxLod = 0;
        info.maxAnisotropy = 1;
        check(vkCreateSampler(device, &info, nullptr, &sampler), "vkCreateSampler");
        info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vkCreateSampler(device, &info, nullptr, &viewportSampler), "vkCreateSampler(viewport)");

        const std::array layouts{sceneLayout, materialLayout};
        VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MaterialPush)};
        VkPipelineLayoutCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineInfo.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
        pipelineInfo.pSetLayouts = layouts.data();
        pipelineInfo.pushConstantRangeCount = 1;
        pipelineInfo.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout(scene)");
        push.size = sizeof(glm::vec2);
        pipelineInfo.setLayoutCount = 1;
        pipelineInfo.pSetLayouts = &materialLayout;
        check(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &postLayout), "vkCreatePipelineLayout(FXAA)");
    }

    VkPipeline createPipeline(VkRenderPass renderPass, VkPipelineLayout layout, VkSampleCountFlagBits samples,
                              bool scene, const char* vertexName, const char* fragmentName) {
        const auto vertexCode = readShader(shaderDirectory / vertexName),
                   fragmentCode = readShader(shaderDirectory / fragmentName);
        VkPipeline result{};
        VkShaderModule vertex{}, fragment{};
        auto create = [&](const std::vector<std::uint32_t>& code, VkShaderModule& output) {
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = code.size() * 4;
            info.pCode = code.data();
            check(vkCreateShaderModule(device, &info, nullptr, &output), "vkCreateShaderModule");
        };
        create(vertexCode, vertex);
        try {
            create(fragmentCode, fragment);
            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertex;
            stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragment;
            stages[1].pName = "main";
            VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
            const std::array<VkVertexInputAttributeDescription, 3> attributes{
                {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex, position))},
                 {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex, normal))},
                 {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex, uv))}}};
            VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            input.vertexBindingDescriptionCount = scene ? 1u : 0u;
            input.pVertexBindingDescriptions = &binding;
            input.vertexAttributeDescriptionCount = scene ? 3u : 0u;
            input.pVertexAttributeDescriptions = attributes.data();
            VkPipelineInputAssemblyStateCreateInfo assembly{
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1;
            VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            multisample.rasterizationSamples = samples;
            VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
            depth.depthTestEnable = scene ? VK_TRUE : VK_FALSE;
            depth.depthWriteEnable = scene ? VK_TRUE : VK_FALSE;
            depth.depthCompareOp = VK_COMPARE_OP_LESS;
            VkPipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                        VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            blend.attachmentCount = 1;
            blend.pAttachments = &attachment;
            const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = dynamicStates.data();
            VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            info.stageCount = 2;
            info.pStages = stages.data();
            info.pVertexInputState = &input;
            info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewportState;
            info.pRasterizationState = &raster;
            info.pMultisampleState = &multisample;
            info.pDepthStencilState = &depth;
            info.pColorBlendState = &blend;
            info.pDynamicState = &dynamic;
            info.layout = layout;
            info.renderPass = renderPass;
            check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &result),
                  "vkCreateGraphicsPipelines");
        } catch (...) {
            if (result) vkDestroyPipeline(device, result, nullptr);
            if (fragment) vkDestroyShaderModule(device, fragment, nullptr);
            vkDestroyShaderModule(device, vertex, nullptr);
            throw;
        }
        vkDestroyShaderModule(device, fragment, nullptr);
        vkDestroyShaderModule(device, vertex, nullptr);
        return result;
    }

    ScenePipeline& scenePipeline(std::uint32_t samples) {
        auto& cached = scenePipelines[std::countr_zero(samples)];
        if (!cached) {
            auto candidate = std::make_unique<ScenePipeline>();
            candidate->device = device;
            candidate->samples = static_cast<VkSampleCountFlagBits>(samples);
            createScenePass(*candidate);
            candidate->pipeline = createPipeline(candidate->pass, pipelineLayout, candidate->samples,
                true, "model.vert.spv", "model.frag.spv");
            cached = std::move(candidate);
        }
        return *cached;
    }

    void initialize(GLFWwindow* target, const std::filesystem::path& shaders, bool requestedValidation) {
        window = target;
        shaderDirectory = shaders;
        createInstance(requestedValidation);
        selectDevice();
        createDevice();
        createSwapchain();
        createDescriptors();
        scenePipeline(1);
        createPostPass();
        postPipeline = createPipeline(postPass, postLayout, VK_SAMPLE_COUNT_1_BIT,
            false, "fullscreen.vert.spv", "fxaa.frag.spv");
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

    void destroyViewport() noexcept {
        viewport.reset();
        sceneRendered = false;
    }

    std::unique_ptr<ViewportTargets> createViewport(std::uint32_t width, std::uint32_t height,
                                                   const AntialiasingSettings& settings) {
        auto& scene = scenePipeline(settings.msaaSamples);
        auto candidate = std::make_unique<ViewportTargets>();
        candidate->device = device;
        candidate->uiPool = uiPool;
        candidate->width = width;
        candidate->height = height;
        candidate->color = resources.image(width, height, sceneFormat,
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                        VK_IMAGE_ASPECT_COLOR_BIT);
        candidate->depth = resources.image(width, height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                        VK_IMAGE_ASPECT_DEPTH_BIT, scene.samples);
        const bool multisampled = settings.msaaSamples > 1;
        if (multisampled)
            candidate->multisample = resources.image(width, height, sceneFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, scene.samples);
        const std::array attachments{multisampled ? candidate->multisample.view : candidate->color.view,
                                    candidate->depth.view, candidate->color.view};
        VkFramebufferCreateInfo frame{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        frame.renderPass = scene.pass;
        frame.attachmentCount = multisampled ? 3u : 2u;
        frame.pAttachments = attachments.data();
        frame.width = width;
        frame.height = height;
        frame.layers = 1;
        check(vkCreateFramebuffer(device, &frame, nullptr, &candidate->sceneFrame), "vkCreateFramebuffer(viewport)");
        if (settings.fxaa) {
            candidate->filtered = resources.image(width, height, sceneFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
            frame.renderPass = postPass;
            frame.attachmentCount = 1;
            frame.pAttachments = &candidate->filtered.view;
            check(vkCreateFramebuffer(device, &frame, nullptr, &candidate->postFrame), "vkCreateFramebuffer(FXAA)");
            VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
            VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pool.maxSets = 1;
            pool.poolSizeCount = 1;
            pool.pPoolSizes = &size;
            check(vkCreateDescriptorPool(device, &pool, nullptr, &candidate->postPool), "vkCreateDescriptorPool(FXAA)");
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.descriptorPool = candidate->postPool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &materialLayout;
            check(vkAllocateDescriptorSets(device, &allocate, &candidate->postDescriptor), "vkAllocateDescriptorSets(FXAA)");
            VkDescriptorImageInfo image{viewportSampler, candidate->color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = candidate->postDescriptor;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &image;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
        candidate->descriptor = ImGui_ImplVulkan_AddTexture(viewportSampler, candidate->output().view,
                                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!candidate->descriptor) throw std::runtime_error("Cannot allocate ImGui viewport descriptor");
        return candidate;
    }

    void resizeViewport(std::uint32_t width, std::uint32_t height) {
        width = std::clamp(width, 1u, maximumViewport);
        height = std::clamp(height, 1u, maximumViewport);
        if (viewport && width == viewport->width && height == viewport->height) return;
        check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(viewport)");
        auto replacement = createViewport(width, height, aa);
        viewport.swap(replacement);
        sceneRendered = false;
    }

    void setAntialiasing(const AntialiasingSettings& requested) {
        if (requested.msaaSamples != 1 && requested.msaaSamples != 2 &&
            requested.msaaSamples != 4 && requested.msaaSamples != 8)
            throw std::invalid_argument("MSAA sample count must be 1, 2, 4 or 8");
        AntialiasingSettings effective{1, requested.fxaa};
        for (const auto samples : {8u, 4u, 2u, 1u})
            if (samples <= requested.msaaSamples && (sampleMask & samples)) {
                effective.msaaSamples = samples;
                break;
            }
        if (effective == aa) return;
        check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(antialiasing)");
        auto replacement = createViewport(viewport->width, viewport->height, effective);
        viewport.swap(replacement);
        aa = effective;
        sceneRendered = false;
    }

    void uploadModel(const ModelData& source) {
        if (source.meshes.empty()) throw std::runtime_error("Model has no renderable meshes");
        check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(model)");
        // Build a complete replacement first. Allocation errors preserve the old GPU model.
        auto replacement = std::make_unique<GpuModel>();
        replacement->device = device;
        const auto materialCount = std::max<std::size_t>(1, source.materials.size());
        if (materialCount > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("Too many materials");
        const auto count = static_cast<std::uint32_t>(materialCount);
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = count;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &pool, nullptr, &replacement->pool), "vkCreateDescriptorPool(materials)");
        replacement->materials.reserve(materialCount);
        const MaterialData fallback{};
        for (std::size_t i = 0; i < materialCount; ++i) {
            const auto& material = source.materials.empty() ? fallback : source.materials[i];
            GpuMaterial gpu;
            gpu.image = resources.texture(material.texture);
            gpu.parameters.color = material.baseColor;
            gpu.parameters.alphaMasked = material.alphaMasked ? 1u : 0u;
            gpu.parameters.alphaCutoff = material.alphaCutoff;
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.descriptorPool = replacement->pool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &materialLayout;
            check(vkAllocateDescriptorSets(device, &allocate, &gpu.descriptor), "vkAllocateDescriptorSets(material)");
            VkDescriptorImageInfo image{sampler, gpu.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = gpu.descriptor;
            write.dstBinding = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &image;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            replacement->materials.push_back(std::move(gpu));
        }
        replacement->meshes.reserve(source.meshes.size());
        for (const auto& mesh : source.meshes) {
            if (mesh.vertices.empty() || mesh.indices.empty() ||
                mesh.indices.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("Invalid or oversized mesh");
            if (mesh.materialIndex >= materialCount || mesh.indices.size() % 3 ||
                std::ranges::any_of(mesh.indices, [&](auto index) { return index >= mesh.vertices.size(); }))
                throw std::runtime_error("Invalid mesh indices or material");
            GpuMesh gpu;
            gpu.vertices = resources.upload<Vertex>(mesh.vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            gpu.indices = resources.upload<std::uint32_t>(mesh.indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            gpu.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
            gpu.material = mesh.materialIndex;
            replacement->meshes.push_back(std::move(gpu));
        }
        model = std::move(replacement);
    }

    void drawScene(const RenderSettings& settings) {
        const auto viewportWidth = viewport->width, viewportHeight = viewport->height;
        const auto& scene = scenePipeline(aa.msaaSamples);
        SceneUniform uniform;
        uniform.viewProjection = settings.viewProjection;
        uniform.model = settings.model;
        auto light = settings.lightDirection;
        if (glm::dot(light, light) < 0.000001f) light = {0, -1, 0};
        uniform.lightDirection = glm::vec4(light, 0);
        uniform.parameters = {std::clamp(settings.ambient, 0.0f, 1.0f), settings.unlit ? 1.0f : 0.0f, 0, 0};
        std::memcpy(uniformBuffer.mapped, &uniform, sizeof(uniform));
        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{settings.background.r, settings.background.g, settings.background.b, 1}};
        clears[1].depthStencil = {1, 0};
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = scene.pass;
        begin.framebuffer = viewport->sceneFrame;
        begin.renderArea = {{0, 0}, {viewportWidth, viewportHeight}};
        begin.clearValueCount = 2;
        begin.pClearValues = clears.data();
        vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewportArea{0, 0, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0, 1};
        VkRect2D scissor{{0, 0}, {viewportWidth, viewportHeight}};
        vkCmdSetViewport(command, 0, 1, &viewportArea);
        vkCmdSetScissor(command, 0, 1, &scissor);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, scene.pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sceneDescriptor, 0,
                                nullptr);
        if (model)
            for (const auto& mesh : model->meshes) {
                const auto& material = model->materials[mesh.material];
                constexpr VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(command, 0, 1, &mesh.vertices.handle, &offset);
                vkCmdBindIndexBuffer(command, mesh.indices.handle, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1,
                                        &material.descriptor, 0, nullptr);
                vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MaterialPush),
                                   &material.parameters);
                vkCmdDrawIndexed(command, mesh.indexCount, 1, 0, 0, 0);
            }
        vkCmdEndRenderPass(command);
        sceneRendered = true;
    }

    void drawFxaa() {
        if (!aa.fxaa) return;
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass.renderPass = postPass;
        pass.framebuffer = viewport->postFrame;
        pass.renderArea = {{0, 0}, {viewport->width, viewport->height}};
        vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport target{0, 0, static_cast<float>(viewport->width), static_cast<float>(viewport->height), 0, 1};
        VkRect2D scissor{{0, 0}, {viewport->width, viewport->height}};
        vkCmdSetViewport(command, 0, 1, &target);
        vkCmdSetScissor(command, 0, 1, &scissor);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout, 0, 1,
                                &viewport->postDescriptor, 0, nullptr);
        const glm::vec2 reciprocal{1.0f / static_cast<float>(viewport->width),
                                   1.0f / static_cast<float>(viewport->height)};
        vkCmdPushConstants(command, postLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(reciprocal), &reciprocal);
        vkCmdDraw(command, 3, 1, 0, 0);
        vkCmdEndRenderPass(command);
    }

    void drawFrame(RenderSettings& settings, const std::function<void()>& ui) {
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

    void captureViewport(const std::filesystem::path& path) {
        if (!sceneRendered) throw std::runtime_error("Cannot capture before the first rendered scene");
        const auto viewportWidth = viewport->width, viewportHeight = viewport->height;
        const auto& viewportColor = viewport->output();
        check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(capture)");
        const auto bytes = static_cast<VkDeviceSize>(viewportWidth) * viewportHeight * 4;
        auto staging =
            resources.buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
        resources.immediate([&](VkCommandBuffer cmd) {
            detail::barrier(cmd, viewportColor.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                            VK_ACCESS_TRANSFER_READ_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {viewportWidth, viewportHeight, 1};
            vkCmdCopyImageToBuffer(cmd, viewportColor.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.handle, 1,
                                   &copy);
            detail::barrier(cmd, viewportColor.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            host.buffer = staging.handle;
            host.size = bytes;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                                 &host, 0, nullptr);
        });
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        if (!output) throw std::runtime_error("Cannot write scene capture: " + pathText(path));
        output << "P6\n" << viewportWidth << ' ' << viewportHeight << "\n255\n";
        const auto* pixels = static_cast<const unsigned char*>(staging.mapped);
        std::vector<unsigned char> rgb(static_cast<std::size_t>(viewportWidth) * viewportHeight * 3);
        for (std::size_t i = 0, j = 0; i < static_cast<std::size_t>(bytes); i += 4, j += 3)
            for (std::size_t c = 0; c < 3; ++c) {
                const float linear = pixels[i + c] / 255.0f;
                const float srgb =
                    linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
                rgb[j + c] = static_cast<unsigned char>(std::clamp(srgb * 255.0f, 0.0f, 255.0f) + 0.5f);
            }
        output.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        if (!output) throw std::runtime_error("Failed writing scene capture");
        std::cout << "Scene capture: " << pathText(path) << '\n';
    }

    void shutdown() noexcept {
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
            if (postPipeline) vkDestroyPipeline(device, postPipeline, nullptr);
            if (postLayout) vkDestroyPipelineLayout(device, postLayout, nullptr);
            if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (uniformPool) vkDestroyDescriptorPool(device, uniformPool, nullptr);
            if (uiPool) vkDestroyDescriptorPool(device, uiPool, nullptr);
            if (sceneLayout) vkDestroyDescriptorSetLayout(device, sceneLayout, nullptr);
            if (materialLayout) vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
            if (sampler) vkDestroySampler(device, sampler, nullptr);
            if (viewportSampler) vkDestroySampler(device, viewportSampler, nullptr);
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
};

VulkanRenderer::VulkanRenderer(GLFWwindow* window, const std::filesystem::path& shaders, bool validation)
    : impl_(std::make_unique<Impl>()) {
    impl_->initialize(window, shaders, validation);
}

VulkanRenderer::~VulkanRenderer() = default;

void VulkanRenderer::uploadModel(const ModelData& model) { impl_->uploadModel(model); }

void VulkanRenderer::setAntialiasing(const AntialiasingSettings& settings) { impl_->setAntialiasing(settings); }
AntialiasingSettings VulkanRenderer::antialiasing() const { return impl_->aa; }
std::uint32_t VulkanRenderer::supportedMsaaSamples() const { return impl_->sampleMask; }

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
