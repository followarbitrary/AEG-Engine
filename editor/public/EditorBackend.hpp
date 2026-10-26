#pragma once
#include <GuiRenderer.hpp>
#include <Renderer.hpp>
#include <RhiSwapchain.hpp>
#include <filesystem>
#include <functional>
#include <memory>
struct GLFWwindow;
namespace aeg::editor {
struct EditorBackend {
    std::unique_ptr<rhi::Device> device;
    std::unique_ptr<rhi::Swapchain> swapchain;
    std::unique_ptr<gui::GuiRenderer> gui;
};
using EditorBackendFactory = std::function<EditorBackend(const rhi::NativeWindow&, bool validation)>;
class EditorGraphics {
public:
    EditorGraphics(GLFWwindow* window, const std::filesystem::path& shaderDirectory,
                   const EditorBackendFactory& factory, bool validation = true);
    ~EditorGraphics();
    EditorGraphics(const EditorGraphics&) = delete;
    EditorGraphics& operator=(const EditorGraphics&) = delete;
    [[nodiscard]] render::Renderer& renderer();
    [[nodiscard]] ImTextureID viewportTexture();
    void drawFrame(render::RenderSettings& settings, const std::function<void()>& buildUi);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
