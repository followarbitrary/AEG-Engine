#include "VulkanRendererImpl.hpp"

namespace aeg::render {
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::Impl::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
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

VkDebugUtilsMessengerCreateInfoEXT VulkanRenderer::Impl::debugInfo() {
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

void VulkanRenderer::Impl::createInstance(bool requestedValidation) {
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

void VulkanRenderer::Impl::selectDevice() {
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
    maximumTemporalViewport = maximumViewport;
    temporalSupported = properties.limits.maxColorAttachments >= 2;
    for (const auto candidate : {motionFormat, historyFormat, historyDepthFormat}) {
        VkImageFormatProperties supported{};
        const auto result = vkGetPhysicalDeviceImageFormatProperties(physical, candidate, VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0, &supported);
        if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
            temporalSupported = false;
            continue;
        }
        check(result, "vkGetPhysicalDeviceImageFormatProperties(TAA)");
        if (!(supported.sampleCounts & VK_SAMPLE_COUNT_1_BIT)) temporalSupported = false;
        maximumTemporalViewport = std::min({maximumTemporalViewport, supported.maxExtent.width,
                                            supported.maxExtent.height});
        VkFormatProperties features{};
        vkGetPhysicalDeviceFormatProperties(physical, candidate, &features);
        if (candidate == historyFormat &&
            !(features.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
            temporalSupported = false;
    }
    std::cout << "Supported MSAA mask: " << sampleMask << '\n';
}

void VulkanRenderer::Impl::createDevice() {
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
}
