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
