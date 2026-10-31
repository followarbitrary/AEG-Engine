#include "VulkanBackendInternal.hpp"

namespace aeg::rhi::vulkan::detail {
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                           VkDebugUtilsMessageTypeFlagsEXT,
                                                           const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                           void* user) {
    auto& self = *static_cast<VulkanDevice*>(user);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++self.errors;
        std::cerr << "[Vulkan error] " << data->pMessage << '\n';
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "[Vulkan warning] " << data->pMessage << '\n';
    }
    return VK_FALSE;
}
VkDebugUtilsMessengerCreateInfoEXT VulkanDevice::debugInfo() {
    VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
    info.pUserData = this;
    return info;
}
VulkanDevice::VulkanDevice(const DeviceDesc& description) : creationDesc(description) {
    if (creationDesc.window.system != WindowSystem::Win32 || !creationDesc.window.window ||
        !creationDesc.window.application || !creationDesc.window.framebufferExtent)
        throw std::invalid_argument("Vulkan requires a valid Win32 native window and extent callback");
    try {
        createInstance();
        selectDevice();
        createLogicalDevice();
    } catch (...) {
        cleanup();
        throw;
    }
}
VulkanDevice::~VulkanDevice() { cleanup(); }
void VulkanDevice::deferUntilIdle(std::function<void()> callback) {
    if (!callback) throw std::invalid_argument("Missing deferred destruction callback");
    deferredCallbacks.push_back(std::move(callback));
}
void VulkanDevice::drainDeferred() noexcept {
    auto callbacks = std::exchange(deferredCallbacks, {});
    for (auto& callback : callbacks) {
        try {
            callback();
        } catch (...) {
        }
    }
}
void VulkanDevice::waitIdle() {
    check(vkDeviceWaitIdle(handle), "vkDeviceWaitIdle");
    drainDeferred();
    resetDescriptorPools();
}
void VulkanDevice::resetDescriptorPools() {
    if (!handle) return;
    for (auto& page : descriptorPages) {
        if (!page->pool || page->activeSets) continue;
        check(vkResetDescriptorPool(handle, page->pool, 0), "vkResetDescriptorPool");
    }
}
void VulkanDevice::cleanup() noexcept {
    if (handle) {
        vkDeviceWaitIdle(handle);
        drainDeferred();
        // A whole-page reset is only legal after the device is idle and when
        // no live binding set refers to that page. Live pages are left for
        // vkDestroyDevice, which invalidates them as part of device teardown.
        for (auto& page : descriptorPages) {
            if (!page->pool) continue;
            if (!page->activeSets) {
                vkResetDescriptorPool(handle, page->pool, 0);
                vkDestroyDescriptorPool(handle, page->pool, nullptr);
                page->pool = VK_NULL_HANDLE;
            }
        }
        if (allocator) {
            vmaDestroyAllocator(allocator);
            allocator = VK_NULL_HANDLE;
        }
        if (pool) vkDestroyCommandPool(handle, pool, nullptr);
        vkDestroyDevice(handle, nullptr);
        handle = VK_NULL_HANDLE;
        pool = VK_NULL_HANDLE;
        for (auto& page : descriptorPages) page->pool = VK_NULL_HANDLE;
    }
    if (surface) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    if (messenger) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance, messenger, nullptr);
        messenger = VK_NULL_HANDLE;
    }
    if (instance) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}
void VulkanDevice::createInstance() {
    std::uint32_t version = VK_API_VERSION_1_0;
    auto enumerate =
        reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
    if (enumerate) check(enumerate(&version), "vkEnumerateInstanceVersion");
    if (version < VK_API_VERSION_1_2) throw std::runtime_error("Vulkan 1.2 or newer is required");
    std::vector<const char*> extensions{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    std::uint32_t count{};
    check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
          "vkEnumerateInstanceExtensionProperties(global)");
    std::vector<VkExtensionProperties> instanceExtensions(count);
    check(vkEnumerateInstanceExtensionProperties(nullptr, &count, instanceExtensions.data()),
          "vkEnumerateInstanceExtensionProperties(global)");
    debugUtils = std::ranges::any_of(instanceExtensions, [](const auto& value) {
        return std::string_view(value.extensionName) == VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    });
    if (debugUtils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    check(vkEnumerateInstanceLayerProperties(&count, nullptr), "vkEnumerateInstanceLayerProperties");
    std::vector<VkLayerProperties> layers(count);
    check(vkEnumerateInstanceLayerProperties(&count, layers.data()), "vkEnumerateInstanceLayerProperties");
    const char* layer = "VK_LAYER_KHRONOS_validation";
    validation = creationDesc.validation && std::ranges::any_of(layers, [&](const auto& value) {
                     return std::string_view(value.layerName) == layer;
                 });
    if (creationDesc.validation && !validation) std::cerr << "Validation layer unavailable; continuing without it.\n";
    if (validation) {
        check(vkEnumerateInstanceExtensionProperties(layer, &count, nullptr), "vkEnumerateInstanceExtensionProperties");
        std::vector<VkExtensionProperties> available(count);
        check(vkEnumerateInstanceExtensionProperties(layer, &count, available.data()),
              "vkEnumerateInstanceExtensionProperties");
        if (!debugUtils) {
            debugUtils = std::ranges::any_of(available, [](const auto& value) {
                return std::string_view(value.extensionName) == VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
            });
            if (debugUtils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        syncValidation = std::ranges::any_of(available, [](const auto& value) {
            return std::string_view(value.extensionName) == VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
        });
        if (syncValidation) extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "AEG Engine";
    app.pEngineName = "AEG";
    app.apiVersion = VK_API_VERSION_1_2;
    auto debug = debugInfo();
    const VkValidationFeatureEnableEXT enabled = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    features.enabledValidationFeatureCount = 1;
    features.pEnabledValidationFeatures = &enabled;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    if (validation && debugUtils) {
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = &layer;
        debug.pNext = syncValidation ? &features : nullptr;
        info.pNext = &debug;
    } else if (validation) {
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = &layer;
        info.pNext = syncValidation ? &features : nullptr;
    }
    check(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");
    if (validation && debugUtils) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!create) throw std::runtime_error("Vulkan debug messenger entry point missing");
        debug.pNext = nullptr;
        check(create(instance, &debug, nullptr, &messenger), "vkCreateDebugUtilsMessengerEXT");
    }
    VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surfaceInfo.hwnd = static_cast<HWND>(creationDesc.window.window);
    surfaceInfo.hinstance = static_cast<HINSTANCE>(creationDesc.window.application);
    check(vkCreateWin32SurfaceKHR(instance, &surfaceInfo, nullptr, &surface), "vkCreateWin32SurfaceKHR");
}
void VulkanDevice::selectDevice() {
    std::uint32_t count{};
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> candidates(count);
    check(vkEnumeratePhysicalDevices(instance, &count, candidates.data()), "vkEnumeratePhysicalDevices");
    int best = -1;
    for (auto candidate : candidates) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(candidate, &props);
        if (props.apiVersion < VK_API_VERSION_1_2) continue;
        check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, nullptr),
              "vkEnumerateDeviceExtensionProperties");
        std::vector<VkExtensionProperties> extensions(count);
        check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, extensions.data()),
              "vkEnumerateDeviceExtensionProperties");
        if (!std::ranges::any_of(extensions, [](const auto& value) {
                return std::string_view(value.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
            }))
            continue;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, queues.data());
        std::optional<std::uint32_t> graphics, present;
        int queueScore = -1;
        for (std::uint32_t i = 0; i < count; ++i) {
            VkBool32 support{};
            check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &support),
                  "vkGetPhysicalDeviceSurfaceSupportKHR");
            if (queues[i].queueCount && support && !present) present = i;
            if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                const int score = ((queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) ? 4 : 0) + (support ? 2 : 0);
                if (score > queueScore) {
                    graphics = i;
                    queueScore = score;
                }
            }
        }
        if (!graphics || !present) continue;
        VkBool32 graphicsPresent{};
        check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, *graphics, surface, &graphicsPresent),
              "vkGetPhysicalDeviceSurfaceSupportKHR(graphics)");
        if (graphicsPresent) present = graphics;
        std::uint32_t formats{}, modes{};
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &formats, nullptr),
              "vkGetPhysicalDeviceSurfaceFormatsKHR");
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &modes, nullptr),
              "vkGetPhysicalDeviceSurfacePresentModesKHR");
        if (!formats || !modes) continue;
        int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 10;
        if (score > best) {
            best = score;
            physical = candidate;
            properties = props;
            graphicsFamily = *graphics;
            presentFamily = *present;
            timestampValidBits = queues[*graphics].timestampValidBits;
            caps.compute = (queues[*graphics].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        }
    }
    if (!physical) throw std::runtime_error("No Vulkan 1.2 GPU with graphics and presentation support found");
    name = properties.deviceName;
    caps.maximumImageDimension = properties.limits.maxImageDimension2D;
    caps.maximumCubeImageDimension = properties.limits.maxImageDimensionCube;
    std::copy_n(properties.limits.maxComputeWorkGroupCount, 3, caps.maximumComputeGroupCount.begin());
    std::copy_n(properties.limits.maxComputeWorkGroupSize, 3, caps.maximumComputeGroupSize.begin());
    caps.maximumComputeInvocations = properties.limits.maxComputeWorkGroupInvocations;
    caps.indirectDraw = properties.limits.maxDrawIndirectCount != 0;
    caps.maximumBindlessSampledDescriptors = std::min<std::uint32_t>(
        256, std::min(properties.limits.maxDescriptorSetSampledImages, properties.limits.maxDescriptorSetSamplers));
    caps.maximumFramebufferDimension =
        std::min(properties.limits.maxFramebufferWidth, properties.limits.maxFramebufferHeight);
    caps.maximumColorAttachments = properties.limits.maxColorAttachments;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    features.pNext = &features11;
    vkGetPhysicalDeviceFeatures2(physical, &features);
    caps.independentBlend = features.features.independentBlend == VK_TRUE;
    caps.samplerAnisotropy = features.features.samplerAnisotropy == VK_TRUE;
    caps.maximumSamplerAnisotropy = caps.samplerAnisotropy ? properties.limits.maxSamplerAnisotropy : 1.0f;
    caps.descriptorIndexing = features.features.shaderSampledImageArrayDynamicIndexing == VK_TRUE;
    caps.shaderDrawParameters = features11.shaderDrawParameters == VK_TRUE;
    for (auto format : {Format::D32Float, Format::D24UnormS8, Format::D16Unorm})
        if (textureSupport(format, TextureUsage::DepthAttachment).supported) {
            caps.depthFormat = format;
            break;
        }
    if (caps.depthFormat == Format::Undefined) throw std::runtime_error("No compatible depth attachment format");
    std::cout << "Vulkan GPU: " << name << " | validation " << (validation ? "on" : "off")
              << (syncValidation ? " (synchronization enabled)" : "")
              << " | debug labels " << (debugUtils ? "on" : "off")
              << " | timestamps " << ((timestampValidBits && properties.limits.timestampPeriod > 0.0f) ? "on" : "off")
              << '\n';
}
void VulkanDevice::createLogicalDevice() {
    const std::set<std::uint32_t> families{graphicsFamily, presentFamily};
    const float priority = 1;
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (auto family : families) {
        VkDeviceQueueCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &priority;
        queues.push_back(info);
    }
    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = static_cast<std::uint32_t>(queues.size());
    info.pQueueCreateInfos = queues.data();
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &extension;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.features.independentBlend = caps.independentBlend ? VK_TRUE : VK_FALSE;
    features.features.samplerAnisotropy = caps.samplerAnisotropy ? VK_TRUE : VK_FALSE;
    features.features.shaderSampledImageArrayDynamicIndexing = caps.descriptorIndexing ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    features11.shaderDrawParameters = caps.shaderDrawParameters ? VK_TRUE : VK_FALSE;
    features.pNext = &features11;
    info.pNext = &features;
    info.pEnabledFeatures = nullptr;
    check(vkCreateDevice(physical, &info, nullptr, &handle), "vkCreateDevice");
    if (debugUtils) {
        cmdBeginDebugUtilsLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetDeviceProcAddr(handle, "vkCmdBeginDebugUtilsLabelEXT"));
        cmdEndDebugUtilsLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetDeviceProcAddr(handle, "vkCmdEndDebugUtilsLabelEXT"));
    }
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = instance;
    allocatorInfo.physicalDevice = physical;
    allocatorInfo.device = handle;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    check(vmaCreateAllocator(&allocatorInfo, &allocator), "vmaCreateAllocator");
    vkGetDeviceQueue(handle, graphicsFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(handle, presentFamily, 0, &presentQueue);
    VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    commandPool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPool.queueFamilyIndex = graphicsFamily;
    check(vkCreateCommandPool(handle, &commandPool, nullptr, &pool), "vkCreateCommandPool");
}
TextureSupport VulkanDevice::textureSupport(Format format, TextureUsage usage, TextureDimension dimension) const {
    if (format == Format::Undefined || usage == TextureUsage::None) return {};
    if (dimension != TextureDimension::Texture2D && dimension != TextureDimension::Cube)
        throw std::invalid_argument("Unknown texture dimension");
    if (contains(usage, TextureUsage::Storage) && !caps.compute) return {};
    const VkImageCreateFlags flags = dimension == TextureDimension::Cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    VkImageFormatProperties info{};
    auto result = vkGetPhysicalDeviceImageFormatProperties(physical, nativeFormat(format), VK_IMAGE_TYPE_2D,
                                                           VK_IMAGE_TILING_OPTIMAL, imageUsage(usage), flags, &info);
    if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) return {};
    check(result, "vkGetPhysicalDeviceImageFormatProperties");
    if (dimension == TextureDimension::Cube && info.maxArrayLayers < 6) return {};
    auto samples = info.sampleCounts;
    if (contains(usage, TextureUsage::ColorAttachment)) samples &= properties.limits.framebufferColorSampleCounts;
    if (contains(usage, TextureUsage::DepthAttachment)) samples &= properties.limits.framebufferDepthSampleCounts;
    VkFormatProperties features{};
    vkGetPhysicalDeviceFormatProperties(physical, nativeFormat(format), &features);
    const auto maximum = dimension == TextureDimension::Cube ? properties.limits.maxImageDimensionCube
                                                             : properties.limits.maxImageDimension2D;
    return {true,
            samples,
            {std::min(info.maxExtent.width, maximum), std::min(info.maxExtent.height, maximum)},
            (features.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0,
            (features.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0};
}
VulkanDevice& nativeDevice(Device& device) {
    auto* native = dynamic_cast<VulkanDevice*>(&device);
    if (!native) throw std::invalid_argument("Device is not a Vulkan RHI device");
    return *native;
}
NativeContext nativeContext(Device& device) {
    auto& native = nativeDevice(device);
    return {native.instance, native.physical, native.handle, native.graphicsQueue, native.graphicsFamily};
}
}
namespace aeg::rhi::vulkan {
std::unique_ptr<Device> createVulkanDevice(const DeviceDesc& desc) {
    return std::make_unique<detail::VulkanDevice>(desc);
}
}
