#pragma once
#include <AntialiasingSettings.hpp>
#include <ModelData.hpp>
#include <RenderSettings.hpp>
#include <ShadowSettings.hpp>
#include <EnvironmentSettings.hpp>
#include <ToneMappingSettings.hpp>
#include <EnvironmentImage.hpp>
#include <RhiDevice.hpp>
#include <filesystem>
#include <memory>
namespace aeg::render {
class Renderer {
public:
    explicit Renderer(rhi::Device& device);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void uploadModel(const assets::ModelData& model);
    void uploadEnvironment(const assets::HdrImageData& image);
    void useProceduralEnvironment();
    void setEnvironment(const EnvironmentSettings& settings);
    [[nodiscard]] EnvironmentSettings environment() const;
    [[nodiscard]] bool supportsEnvironment() const;
    [[nodiscard]] bool hasEnvironment() const;
    void setToneMapping(const ToneMappingSettings& settings);
    [[nodiscard]] ToneMappingSettings toneMapping() const;
    void setAntialiasing(const AntialiasingSettings& settings);
    [[nodiscard]] AntialiasingSettings antialiasing() const;
    [[nodiscard]] std::uint32_t supportedMsaaSamples() const;
    [[nodiscard]] bool supportsTemporalAA() const;
    void setShadows(const ShadowSettings& settings);
    [[nodiscard]] ShadowSettings shadows() const;
    [[nodiscard]] bool supportsShadows() const;
    [[nodiscard]] std::uint32_t maximumShadowResolution() const;
    void resetTemporalHistory();
    [[nodiscard]] std::uint32_t temporalFrameCount() const;
    void resizeViewport(std::uint32_t width, std::uint32_t height);
    // Keeps the image/view alive while a GUI registration refers to it.
    [[nodiscard]] std::shared_ptr<rhi::Texture> outputTexture() const;
    void recordFrame(rhi::CommandList& commands, const RenderSettings& settings);
    void frameSubmitted();
    void captureViewport(const std::filesystem::path& output);
    void waitIdle();
    [[nodiscard]] const std::string& deviceName() const;
    [[nodiscard]] std::uint32_t validationErrorCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
