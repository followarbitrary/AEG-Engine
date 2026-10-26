#include "RendererHarness.hpp"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
enum class Failure { DescriptorLayout, FontAllocation, FontMap, FontFlush };
Failure failure{};
bool injected{};
bool stagingCreated{};
VkBuffer stagingBuffer{};
VkDeviceMemory stagingMemory{};

bool rendererRegistered() { return ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData; }
template <class Function> Function deviceFunction(VkDevice device, const char* name) {
    return reinterpret_cast<Function>(vkGetDeviceProcAddr(device, name));
}
}

// Intercept real loader calls in this executable. The layout case fails after
// backend registration; the font cases fail after staging buffer creation.
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice device,
                                                                      const VkDescriptorSetLayoutCreateInfo* info,
                                                                      const VkAllocationCallbacks* allocation,
                                                                      VkDescriptorSetLayout* output) {
    if (!injected && failure == Failure::DescriptorLayout && rendererRegistered()) {
        injected = true;
        *output = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return deviceFunction<PFN_vkCreateDescriptorSetLayout>(device, "vkCreateDescriptorSetLayout")(device, info,
                                                                                                  allocation, output);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* info,
                                                         const VkAllocationCallbacks* allocation, VkBuffer* output) {
    const auto result = deviceFunction<PFN_vkCreateBuffer>(device, "vkCreateBuffer")(device, info, allocation, output);
    if (result == VK_SUCCESS && rendererRegistered() && info->usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT) {
        stagingCreated = true;
        stagingBuffer = *output;
    }
    return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* info,
                                                           const VkAllocationCallbacks* allocation,
                                                           VkDeviceMemory* output) {
    if (!injected && failure == Failure::FontAllocation && stagingBuffer && !stagingMemory) {
        injected = true;
        *output = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    const auto result =
        deviceFunction<PFN_vkAllocateMemory>(device, "vkAllocateMemory")(device, info, allocation, output);
    if (result == VK_SUCCESS && stagingBuffer && !stagingMemory) stagingMemory = *output;
    return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
                                                      VkDeviceSize size, VkMemoryMapFlags flags, void** output) {
    if (!injected && failure == Failure::FontMap && memory == stagingMemory && stagingMemory) {
        injected = true;
        *output = nullptr;
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    return deviceFunction<PFN_vkMapMemory>(device, "vkMapMemory")(device, memory, offset, size, flags, output);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice device, std::uint32_t count,
                                                                    const VkMappedMemoryRange* ranges) {
    if (!injected && failure == Failure::FontFlush && count && ranges[0].memory == stagingMemory && stagingMemory) {
        injected = true;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return deviceFunction<PFN_vkFlushMappedMemoryRanges>(device, "vkFlushMappedMemoryRanges")(device, count, ranges);
}

namespace {
bool runCase(Failure selected, const char* name) {
    failure = selected;
    injected = stagingCreated = false;
    stagingBuffer = VK_NULL_HANDLE;
    stagingMemory = VK_NULL_HANDLE;
    auto* window = glfwCreateWindow(640, 480, "AEG backend failure test", nullptr, nullptr);
    if (!window) return false;
    ImGui::CreateContext();
    std::ostringstream diagnostics;
    auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
    bool threw = false;
    try {
        aeg::tests::TestRenderer renderer(window, true);
    } catch (const std::exception&) {
        threw = true;
    }
    const bool cleaned = ImGui::GetIO().BackendRendererUserData == nullptr;
    const bool platformCleaned = ImGui::GetIO().BackendPlatformUserData == nullptr;
    // A failed implementation already destroyed its device; detach stale state
    // only to allow test-process teardown, while still failing the assertions.
    if (!cleaned) {
        ImGui::GetIO().BackendRendererUserData = nullptr;
        ImGui::GetIO().BackendRendererName = nullptr;
    }
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    std::cerr.rdbuf(previous);
    const bool valid = diagnostics.str().find("[Vulkan error]") == std::string::npos;
    const bool reached = selected == Failure::DescriptorLayout || stagingCreated;
    if (!injected || !threw || !cleaned || !platformCleaned || !valid || !reached) {
        std::cerr << "FAIL partial backend initialization (" << name << "): injected=" << injected << " threw=" << threw
                  << " cleaned=" << cleaned << " platformCleaned=" << platformCleaned
                  << " stagingCreated=" << stagingCreated << " valid=" << valid << '\n'
                  << diagnostics.str();
        return false;
    }
    std::cout << "PASS partial ImGui initialization (" << name << ") unwinds without live Vulkan children\n";
    return true;
}
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    bool passed = runCase(Failure::DescriptorLayout, "descriptor layout");
    passed = runCase(Failure::FontAllocation, "font staging allocation") && passed;
    passed = runCase(Failure::FontMap, "font staging map") && passed;
    passed = runCase(Failure::FontFlush, "font staging flush") && passed;
    glfwTerminate();
    return passed ? 0 : 1;
}
