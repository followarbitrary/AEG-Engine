#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include <imgui.h>
#include <aeg/Model.hpp>

struct GLFWwindow;
namespace aeg {
struct AntialiasingSettings {
    std::uint32_t msaaSamples{1};
    bool fxaa{false};
    bool taa{false};
    bool operator==(const AntialiasingSettings&) const = default;
};
struct RenderSettings {
    glm::mat4 model{1};
    glm::mat4 viewProjection{1};
    glm::vec3 lightDirection{-0.5f, -1.0f, -0.4f};
    float ambient{0.28f};
    glm::vec4 background{0.035f, 0.045f, 0.065f, 1};
    bool unlit{false};
};
class VulkanRenderer {
public:
    VulkanRenderer(GLFWwindow* window, const std::filesystem::path& shaderDirectory, bool validation = true);
    ~VulkanRenderer();
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    void uploadModel(const ModelData& model);
    // Set before recording any ImGui::Image of this viewport. Valid sample
    // requests are 1/2/4/8; unsupported counts fall back to the next lower one.
    // TAA uses 1x sampling in place of MSAA; FXAA may follow either path.
    void setAntialiasing(const AntialiasingSettings& settings);
    [[nodiscard]] AntialiasingSettings antialiasing() const;
    // Bitmask of supported sample counts (1 | 2 | 4 | 8).
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
