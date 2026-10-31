#pragma once
#include <GuiRenderer.hpp>
#include <Renderer.hpp>
#include <RhiSwapchain.hpp>
#include <functional>
#include <memory>
#include <string>
#include <span>
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
    EditorGraphics(GLFWwindow* window, const EditorBackendFactory& factory, bool validation = true);
    ~EditorGraphics();
    EditorGraphics(const EditorGraphics&) = delete;
    EditorGraphics& operator=(const EditorGraphics&) = delete;
    [[nodiscard]] render::Renderer& renderer();
    [[nodiscard]] ImTextureID viewportTexture();
    [[nodiscard]] const std::string& renderGraphDump() const;
    [[nodiscard]] std::span<const rhi::GpuPassTiming> gpuPassTimings() const;
    void drawFrame(render::RenderSettings& settings, const std::function<void()>& buildUi);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
