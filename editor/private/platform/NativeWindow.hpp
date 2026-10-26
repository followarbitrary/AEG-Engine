#pragma once
#include <RhiTypes.hpp>
struct GLFWwindow;
namespace aeg::editor {
rhi::NativeWindow nativeWindow(GLFWwindow* window);
}
