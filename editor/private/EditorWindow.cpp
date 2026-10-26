#include "EditorState.hpp"
#include <Utf8Path.hpp>
#include <GLFW/glfw3.h>

#include <exception>
#include <iostream>
#include <stdexcept>

namespace aeg::editor {
WindowLifetime::~WindowLifetime() {
    if (context) ImGui::DestroyContext(context);
    if (window) glfwDestroyWindow(window);
    if (initialized) glfwTerminate();
}

void initializeEditorWindow(WindowLifetime& lifetime, const EditorOptions& options) {
    glfwSetErrorCallback([](int code, const char* text) { std::cerr << "GLFW " << code << ": " << text << '\n'; });
    if (!glfwInit()) throw std::runtime_error("GLFW 初始化失败。");
    lifetime.initialized = true;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, options.hidden ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, options.hidden ? GLFW_FALSE : GLFW_TRUE);
    lifetime.window = glfwCreateWindow(1280, 800, "AEG Engine | 模型工作室", nullptr, nullptr);
    if (!lifetime.window) throw std::runtime_error("无法创建编辑器窗口。");
    IMGUI_CHECKVERSION();
    lifetime.context = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
}

void bindImportQueue(GLFWwindow* window, ImportQueue& imports) {
    glfwSetWindowUserPointer(window, &imports);
    glfwSetDropCallback(window, [](GLFWwindow* window, int count, const char** paths) {
        auto& queue = *static_cast<ImportQueue*>(glfwGetWindowUserPointer(window));
        try {
            for (int i = 0; i < count; ++i) queue.paths.push_back(foundation::pathFromUtf8(paths[i]));
        } catch (const std::exception& error) {
            queue.error = error.what();
        }
    });
}
}
