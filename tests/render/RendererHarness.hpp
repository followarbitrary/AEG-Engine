#pragma once
#include <EditorBackend.hpp>
#include <VulkanBackend.hpp>
#include <VulkanGuiBackend.hpp>

namespace aeg::tests {
// Preserve the GPU suites' convenience vocabulary while exercising the same
// coordinator, renderer and backend objects as the application.
class TestRenderer {
public:
    TestRenderer(GLFWwindow* window, bool validation)
        : graphics_(
              window,
              [](const rhi::NativeWindow& native, bool checked) {
                  editor::EditorBackend backend;
                  backend.device = rhi::vulkan::createVulkanDevice({native, checked});
                  backend.swapchain = rhi::vulkan::createVulkanSwapchain(*backend.device);
                  backend.gui = rhi::vulkan::createVulkanGuiRenderer(*backend.device, *backend.swapchain);
                  return backend;
              },
              validation) {
        // Existing analytical material/light tests explicitly exercise linear
        // output. The dedicated HDR suite tests the renderer's ACES default.
        renderer().setToneMapping({render::ToneMapper::Linear, 0});
    }
    void uploadModel(const assets::ModelData& model) { renderer().uploadModel(model); }
    void setAntialiasing(const render::AntialiasingSettings& settings) { renderer().setAntialiasing(settings); }
    void setRenderDebugSettings(const render::RenderDebugSettings& settings) { renderer().setRenderDebugSettings(settings); }
    render::AntialiasingSettings antialiasing() { return renderer().antialiasing(); }
    std::uint32_t supportedMsaaSamples() { return renderer().supportedMsaaSamples(); }
    bool supportsTemporalAA() { return renderer().supportsTemporalAA(); }
    void setShadows(const render::ShadowSettings& settings) { renderer().setShadows(settings); }
    render::ShadowSettings shadows() { return renderer().shadows(); }
    bool supportsShadows() { return renderer().supportsShadows(); }
    std::uint32_t maximumShadowResolution() { return renderer().maximumShadowResolution(); }
    void resetTemporalHistory() { renderer().resetTemporalHistory(); }
    std::uint32_t temporalFrameCount() { return renderer().temporalFrameCount(); }
    void resizeViewport(std::uint32_t width, std::uint32_t height) { renderer().resizeViewport(width, height); }
    ImTextureID viewportTexture() { return graphics_.viewportTexture(); }
    void drawFrame(render::RenderSettings& settings, const std::function<void()>& ui) {
        graphics_.drawFrame(settings, ui);
    }
    void captureViewport(const std::filesystem::path& path) { renderer().captureViewport(path); }
    void waitIdle() { renderer().waitIdle(); }
    const std::string& deviceName() { return renderer().deviceName(); }
    std::uint32_t validationErrorCount() { return renderer().validationErrorCount(); }
    const std::string& renderGraphDump() const { return graphics_.renderGraphDump(); }
    const std::string& rendererGraphDump() { return renderer().renderGraphDump(); }

    render::Renderer& renderer() { return graphics_.renderer(); }

private:
    editor::EditorGraphics graphics_;
};
}
