#include "NativeWindow.hpp"
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#else
#error The editor native-window adapter currently supports Windows only
#endif
#include <stdexcept>

namespace aeg::editor {
rhi::NativeWindow nativeWindow(GLFWwindow* window) {
    if (!window) throw std::invalid_argument("Editor graphics requires a GLFW window");
    rhi::NativeWindow result;
    result.system = rhi::WindowSystem::Win32;
    result.window = glfwGetWin32Window(window);
    result.application = GetModuleHandleW(nullptr);
    result.framebufferExtent = [window] {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        return rhi::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    };
    return result;
}
}
