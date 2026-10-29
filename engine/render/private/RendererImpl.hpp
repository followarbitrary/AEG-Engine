#pragma once
#include <Renderer.hpp>
#include "scene/GpuModel.hpp"
#include "scene/ScenePipeline.hpp"
#include "scene/SceneUniform.hpp"
#include "shadow/ShadowResources.hpp"
#include "viewport/ViewportTargets.hpp"
#include "antialiasing/TemporalHistory.hpp"
#include "shaders/ShaderLibrary.hpp"
#include "environment/EnvironmentBaker.hpp"
#include "rendergraph/TransientTexturePool.hpp"
#include <RenderGraph.hpp>
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
inline constexpr auto displayFormat = rhi::Format::Rgba8Unorm;
inline constexpr auto motionFormat = rhi::Format::Rgba32Float;
inline constexpr auto historyFormat = rhi::Format::Rgba16Float;
inline constexpr auto historyDepthFormat = rhi::Format::R32Float;
inline constexpr auto outputUsage =
    rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySource;

struct EnvironmentResources {
    EnvironmentMaps maps;
    std::unique_ptr<rhi::BindingSet> descriptor;
};

struct Renderer::Impl {
    rhi::Device& device;
    TransientTexturePool graphPool;
    FrameToken graphFrame{};
    rhi::Texture* graphIntermediate{};
    std::shared_ptr<rhi::Texture> graphFiltered;
    std::unique_ptr<rhi::Framebuffer> graphPostFrame;
    std::unique_ptr<rhi::BindingSet> graphPostDescriptor;
    std::string graphDump;
    AntialiasingSettings aa;
    ToneMappingSettings toneMappingSettings;
    rhi::Format sceneFormat{rhi::Format::Undefined};
    std::uint32_t sampleMask{1}, maximumViewport{1}, maximumTemporalViewport{1};
    bool temporalSupported{};
    ShadowSettings shadowSettings;
    bool shadowSupported{};
    EnvironmentSettings environmentSettings;
    bool environmentSupported{};
    std::uint32_t maximumShadow{};
    rhi::Format shadowFormat{rhi::Format::Undefined};
    TemporalHistory temporalHistory, recordingHistory;
    std::unique_ptr<rhi::BindingLayout> sceneLayout, materialLayout, shadowSetLayout, postSetLayout, temporalSetLayout,
        environmentSetLayout;
    std::unique_ptr<rhi::Sampler> sampler, viewportSampler, pointSampler, environmentSampler;
    std::unique_ptr<rhi::Buffer> uniformBuffer;
    std::unique_ptr<rhi::Texture> fallbackShadowTexture;
    std::unique_ptr<rhi::BindingSet> fallbackShadowDescriptor;
    std::unique_ptr<rhi::BindingSet> sceneDescriptor;
    std::unique_ptr<rhi::Texture> fallbackEnvironmentCube, fallbackEnvironmentLut;
    std::unique_ptr<rhi::BindingSet> fallbackEnvironmentDescriptor;
    std::unique_ptr<rhi::PipelineLayout> pipelineLayout, shadowPipelineLayout, postLayout, temporalLayout, toneLayout;
    std::unique_ptr<rhi::RenderPass> postPass, temporalPass, tonePass;
    std::unique_ptr<rhi::GraphicsPipeline> postPipeline, temporalPipeline, tonePipeline;
    std::array<std::unique_ptr<ScenePipeline>, 4> scenePipelines;
    std::unique_ptr<ScenePipeline> temporalScene;
    std::unique_ptr<GpuModel> model;
    std::unique_ptr<ShadowResources> shadow;
    std::unique_ptr<ViewportTargets> viewport;
    std::unique_ptr<EnvironmentBaker> environmentBaker;
    std::unique_ptr<EnvironmentResources> environmentResources;
    std::shared_ptr<rhi::Texture> submittedOutput, pendingOutput;
    RenderSettings pendingSettings;
    bool pendingFrame{};
    bool retryingFrame{};
    std::array<rhi::ResourceState, 2> pendingHistoryColorState{}, pendingHistoryDepthState{};

    explicit Impl(rhi::Device& target);
    ~Impl() noexcept;
    void initialize();
    void createDescriptors();
    void initializeShadowSupport();
    void setShadows(const ShadowSettings& settings);
    void configureShadowUniform(SceneUniform& uniform, const RenderSettings& settings);
    void drawShadow(rhi::CommandList& commands, const SceneUniform& uniform);
    void createScenePass(ScenePipeline& target);
    void createPostPass();
    void createToneMappingPass();
    void setToneMapping(const ToneMappingSettings& settings);
    std::unique_ptr<rhi::GraphicsPipeline>
    createPipeline(rhi::RenderPass& pass, rhi::PipelineLayout& layout, std::uint32_t samples, bool scene,
                   detail::ShaderId vertexId, detail::ShaderId fragmentId, std::uint32_t colorAttachmentCount = 1,
                   rhi::DepthCompare depthCompare = rhi::DepthCompare::Less, bool transparent = false);
    ScenePipeline& scenePipeline(std::uint32_t samples, bool temporal = false, bool reversed = false,
                                 bool transparent = false);
    void resetTemporalHistory() noexcept;
    void invalidateOutput() noexcept;
    std::unique_ptr<ViewportTargets> createViewport(std::uint32_t width, std::uint32_t height,
                                                    const AntialiasingSettings& settings);
    void resizeViewport(std::uint32_t width, std::uint32_t height);
    void setAntialiasing(const AntialiasingSettings& requested);
    void uploadModel(const ModelData& source);
    void initializeEnvironmentSupport();
    void uploadEnvironment(const assets::HdrImageData& image);
    void setEnvironment(const EnvironmentSettings& settings);
    static bool perspectiveProjection(const aeg::math::Mat4& matrix);
    SceneUniform sceneUniform(const RenderSettings& settings);
    void drawScene(rhi::CommandList& commands, const RenderSettings& settings, const SceneUniform& uniform);
    void drawTemporal(rhi::CommandList& commands);
    void drawToneMapping(rhi::CommandList& commands);
    void drawFxaa(rhi::CommandList& commands);
    void prepareRenderGraph(rhi::CommandList& commands);
    void recordFxaaGraph(rhi::CommandList& commands);
    void recordFrame(rhi::CommandList& commands, const RenderSettings& settings);
    void frameSubmitted();
    std::shared_ptr<rhi::Texture> outputTexture() const;
    void captureViewport(const std::filesystem::path& path);
};
}
