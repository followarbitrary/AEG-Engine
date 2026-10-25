#include <aeg/VulkanRenderer.hpp>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
bool injected = false;
}

// Override one loader symbol in this test executable only. ImGui registers its
// backend state before allocating a layout, and already owns a sampler then.
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice device,
                                                                      const VkDescriptorSetLayoutCreateInfo* info,
                                                                      const VkAllocationCallbacks* allocation,
                                                                      VkDescriptorSetLayout* output) {
    if (!injected && ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData) {
        injected = true;
        *output = VK_NULL_HANDLE;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    const auto real =
        reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(vkGetDeviceProcAddr(device, "vkCreateDescriptorSetLayout"));
    return real(device, info, allocation, output);
}

int main(int argc, char** argv) {
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(640, 480, "AEG backend failure test", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    ImGui::CreateContext();
    std::ostringstream diagnostics;
    auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
    bool threw = false;
    try {
        aeg::VulkanRenderer renderer(window, std::filesystem::path(argv[1]), true);
    } catch (const std::exception&) {
        threw = true;
    }
    std::cerr.rdbuf(previous);
    const bool cleaned = ImGui::GetIO().BackendRendererUserData == nullptr;
    const bool valid = diagnostics.str().find("[Vulkan error]") == std::string::npos;
    // On the unfixed path the device has already been destroyed. Do not call
    // backend shutdown against it; detach only for safe test-process teardown.
    if (!cleaned) {
        ImGui::GetIO().BackendRendererUserData = nullptr;
        ImGui::GetIO().BackendRendererName = nullptr;
    }
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    if (!injected || !threw || !cleaned || !valid) {
        std::cerr << "FAIL partial backend initialization: injected=" << injected << " threw=" << threw
                  << " cleaned=" << cleaned << " valid=" << valid << '\n'
                  << diagnostics.str();
        return 1;
    }
    std::cout << "PASS partial ImGui initialization unwinds without live Vulkan children\n";
    return 0;
}
