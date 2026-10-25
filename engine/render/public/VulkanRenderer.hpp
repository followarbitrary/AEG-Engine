#pragma once
#include <AntialiasingSettings.hpp>
#include <ModelData.hpp>
#include <RenderSettings.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <imgui.h>

struct GLFWwindow;
namespace aeg::render {
class VulkanRenderer {
public:
    VulkanRenderer(GLFWwindow* window, const std::filesystem::path& shaderDirectory, bool validation = true);
    ~VulkanRenderer();
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    void uploadModel(const assets::ModelData& model);
    // Set before recording any ImGui::Image of this viewport. Valid sample
    // requests are 1/2/4/8; unsupported counts fall back to the next lower one.
    // TAA uses 1x sampling in place of MSAA; FXAA may follow either path.
    void setAntialiasing(const AntialiasingSettings& settings);
    [[nodiscard]] AntialiasingSettings antialiasing() const;
    [[nodiscard]] std::uint32_t supportedMsaaSamples() const;
    [[nodiscard]] bool supportsTemporalAA() const;
    // Discard future temporal reuse after a camera cut; displayed pixels remain valid.
    void resetTemporalHistory();
    [[nodiscard]] std::uint32_t temporalFrameCount() const;
    void resizeViewport(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] ImTextureID viewportTexture() const;
    void drawFrame(RenderSettings& settings, const std::function<void()>& buildUi);
    void captureViewport(const std::filesystem::path& output);
    void waitIdle();
    [[nodiscard]] const std::string& deviceName() const;
    [[nodiscard]] std::uint32_t validationErrorCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
