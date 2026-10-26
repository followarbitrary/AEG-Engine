#pragma once
#include <Renderer.hpp>
#include "scene/GpuModel.hpp"
#include "scene/ScenePipeline.hpp"
#include "scene/SceneUniform.hpp"
#include "viewport/ViewportTargets.hpp"
#include "antialiasing/TemporalHistory.hpp"
#include "shaders/ShaderLibrary.hpp"
#include <Utf8Path.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aeg::render {
using assets::MaterialData;
using assets::ModelData;
using assets::Vertex;
inline constexpr auto sceneFormat = rhi::Format::Rgba8Unorm;
inline constexpr auto motionFormat = rhi::Format::Rgba32Float;
inline constexpr auto historyFormat = rhi::Format::Rgba16Float;
inline constexpr auto historyDepthFormat = rhi::Format::R32Float;
inline constexpr auto outputUsage =
    rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySource;

struct Renderer::Impl {
    rhi::Device& device;
    AntialiasingSettings aa;
    std::uint32_t sampleMask{1}, maximumViewport{1}, maximumTemporalViewport{1};
    bool temporalSupported{};
    TemporalHistory temporalHistory, recordingHistory;
    std::unique_ptr<rhi::BindingLayout> sceneLayout, materialLayout, postSetLayout, temporalSetLayout;
    std::unique_ptr<rhi::Sampler> sampler, viewportSampler, pointSampler;
    std::unique_ptr<rhi::Buffer> uniformBuffer;
    std::unique_ptr<rhi::BindingSet> sceneDescriptor;
    std::unique_ptr<rhi::PipelineLayout> pipelineLayout, postLayout, temporalLayout;
    std::unique_ptr<rhi::RenderPass> postPass, temporalPass;
    std::unique_ptr<rhi::GraphicsPipeline> postPipeline, temporalPipeline;
    std::array<std::unique_ptr<ScenePipeline>, 4> scenePipelines;
    std::unique_ptr<ScenePipeline> temporalScene;
    std::unique_ptr<GpuModel> model;
    std::unique_ptr<ViewportTargets> viewport;
    std::shared_ptr<rhi::Texture> submittedOutput, pendingOutput;
    RenderSettings pendingSettings;
    bool pendingFrame{};

    explicit Impl(rhi::Device& target);
    ~Impl() noexcept;
    void initialize();
    void createDescriptors();
    void createScenePass(ScenePipeline& target);
    void createPostPass();
    std::unique_ptr<rhi::GraphicsPipeline> createPipeline(rhi::RenderPass& pass, rhi::PipelineLayout& layout,
                                                          std::uint32_t samples, bool scene, detail::ShaderId vertexId,
                                                          detail::ShaderId fragmentId,
                                                          std::uint32_t colorAttachmentCount = 1);
    ScenePipeline& scenePipeline(std::uint32_t samples, bool temporal = false);
    void resetTemporalHistory() noexcept;
    void invalidateOutput() noexcept;
    std::unique_ptr<ViewportTargets> createViewport(std::uint32_t width, std::uint32_t height,
                                                    const AntialiasingSettings& settings);
    void resizeViewport(std::uint32_t width, std::uint32_t height);
    void setAntialiasing(const AntialiasingSettings& requested);
    void uploadModel(const ModelData& source);
    static bool perspectiveProjection(const glm::mat4& matrix);
    SceneUniform sceneUniform(const RenderSettings& settings);
    void drawScene(rhi::CommandList& commands, const RenderSettings& settings);
    void drawTemporal(rhi::CommandList& commands);
    void pointFinalDescriptorAtTemporalOutput();
    void drawFxaa(rhi::CommandList& commands);
    void recordFrame(rhi::CommandList& commands, const RenderSettings& settings);
    void frameSubmitted();
    std::shared_ptr<rhi::Texture> outputTexture() const;
    void captureViewport(const std::filesystem::path& path);
};
}
