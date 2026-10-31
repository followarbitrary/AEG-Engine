#include "EditorState.hpp"
#include <Utf8Path.hpp>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <commdlg.h>
#include <windows.h>
#endif

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

std::filesystem::path browseModelFile(GLFWwindow* window) {
#ifdef _WIN32
    if (!window) return {};
    std::array<wchar_t, 32768> buffer{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = L"模型文件\0*.obj;*.gltf;*.glb;*.pmx\0所有文件\0*.*\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) return std::filesystem::path(buffer.data());
#else
    (void)window;
#endif
    return {};
}
}
