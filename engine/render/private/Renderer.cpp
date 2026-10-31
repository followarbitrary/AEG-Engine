#include "RendererImpl.hpp"

namespace aeg::render {
Renderer::Impl::Impl(rhi::Device& target) : device(target), graphPool(target) { initialize(); }

Renderer::Impl::~Impl() noexcept {
    // Device remains owned by the application. All dependent resources are
    // destroyed below, even when a lost device makes the wait fail.
    try {
        device.waitIdle();
    } catch (...) {
    }
}

void Renderer::Impl::initialize() {
    const auto& caps = device.capabilities();
    for (const auto format : {rhi::Format::Rgba16Float, rhi::Format::Rgba32Float}) {
        const auto attachment = device.textureSupport(format, rhi::TextureUsage::ColorAttachment);
        const auto sampled = device.textureSupport(format, outputUsage);
        if (attachment.supported && sampled.supported && attachment.colorBlending && sampled.colorBlending &&
            sampled.linearFiltering && (attachment.sampleMask & sampled.sampleMask & 1u) &&
            attachment.maximumExtent.width && attachment.maximumExtent.height && sampled.maximumExtent.width &&
            sampled.maximumExtent.height) {
            sceneFormat = format;
            break;
        }
    }
    if (sceneFormat == rhi::Format::Undefined)
        throw std::runtime_error("HDR scene rendering requires sampled, filterable RGBA16F or RGBA32F attachments "
                                 "with alpha blending and single sampling");
    const auto color = device.textureSupport(sceneFormat, rhi::TextureUsage::ColorAttachment);
    const auto single = device.textureSupport(sceneFormat, outputUsage);
    const auto display = device.textureSupport(displayFormat, outputUsage);
    const auto depth = device.textureSupport(caps.depthFormat, rhi::TextureUsage::DepthAttachment);
    sampleMask = color.sampleMask & depth.sampleMask & 15u;
    if (!rhi::isDepthFormat(caps.depthFormat) || !color.supported || !single.supported || !depth.supported ||
        !(sampleMask & 1u) || !(single.sampleMask & 1u))
        throw std::runtime_error("Scene attachments do not support single sampling");
    if (!single.linearFiltering) throw std::runtime_error("Scene format does not support linear sampling");
    if (!color.colorBlending || !single.colorBlending)
        throw std::runtime_error("Scene format does not support material alpha blending");
    if (!display.supported || !(display.sampleMask & 1u) || !display.linearFiltering)
        throw std::runtime_error("Display output requires sampled, linearly filterable RGBA8 single sampling");
    maximumViewport = std::min({caps.maximumImageDimension, caps.maximumFramebufferDimension, color.maximumExtent.width,
                                color.maximumExtent.height, single.maximumExtent.width, single.maximumExtent.height,
                                display.maximumExtent.width, display.maximumExtent.height, depth.maximumExtent.width,
                                depth.maximumExtent.height, 8192u});
    if (!maximumViewport) throw std::runtime_error("Invalid viewport attachment limits");
    maximumTemporalViewport = maximumViewport;
    temporalSupported = caps.maximumColorAttachments >= 2 && caps.independentBlend;
    for (const auto format : {motionFormat, historyFormat, historyDepthFormat}) {
        const auto supported = device.textureSupport(format, outputUsage);
        if (!supported.supported || !(supported.sampleMask & 1u) ||
            (format == historyFormat && !supported.linearFiltering)) {
            temporalSupported = false;
            continue;
        }
        maximumTemporalViewport =
            std::min({maximumTemporalViewport, supported.maximumExtent.width, supported.maximumExtent.height});
    }
    temporalSupported = temporalSupported && maximumTemporalViewport != 0;
    initializeShadowSupport();
    initializeEnvironmentSupport();
    createDescriptors();
    scenePipeline(1);
    createPostPass();
    createToneMappingPass();
    postPipeline = createPipeline(*postPass, *postLayout, 1, false, detail::ShaderId::FullscreenVertex,
                                  detail::ShaderId::FxaaFragment);
    if (temporalSupported)
        temporalPipeline = createPipeline(*temporalPass, *temporalLayout, 1, false, detail::ShaderId::FullscreenVertex,
                                          detail::ShaderId::TaaFragment, 2);
    resizeViewport(960, 640);
}

void Renderer::Impl::createDescriptors() {
    using enum rhi::BindingType;
    using enum rhi::ShaderStage;
    sceneLayout = device.createBindingLayout({{{0, UniformBuffer, Vertex | Fragment | Compute}}});
    materialLayout =
        device.createBindingLayout({{{0, CombinedTextureSampler, Fragment}, {1, CombinedTextureSampler, Fragment}}});
    shadowSetLayout = device.createBindingLayout({{{0, CombinedTextureSampler, Fragment},
                                                   {1, CombinedTextureSampler, Fragment},
                                                   {2, CombinedTextureSampler, Fragment},
                                                   {3, CombinedTextureSampler, Fragment}}});
    postSetLayout = device.createBindingLayout({{{0, CombinedTextureSampler, Fragment}}});
    environmentSetLayout = device.createBindingLayout({{{0, CombinedTextureSampler, Fragment},
                                                        {1, CombinedTextureSampler, Fragment},
                                                        {2, CombinedTextureSampler, Fragment}}});
    const auto& caps = device.capabilities();
    bindlessSupported = caps.compute && caps.indirectDraw && caps.descriptorIndexing && caps.shaderDrawParameters &&
                        caps.maximumBindlessSampledDescriptors >= kBindlessTextureCapacity;
    if (bindlessSupported) {
        bindlessMaterialLayout = device.createBindingLayout(
            {{{0, CombinedTextureSampler, Fragment, kBindlessTextureCapacity},
              {1, CombinedTextureSampler, Fragment, kBindlessTextureCapacity}}});
        cullLayout = device.createBindingLayout(
            {{{0, StorageBuffer, Compute}, {1, StorageBuffer, Compute}}});
    }
    sampler = device.createSampler({rhi::Filter::Linear, rhi::AddressMode::Repeat});
    viewportSampler = device.createSampler({rhi::Filter::Linear, rhi::AddressMode::ClampToEdge});
    pointSampler = device.createSampler({rhi::Filter::Nearest, rhi::AddressMode::ClampToEdge});
    environmentSampler = device.createSampler({rhi::Filter::Linear, rhi::AddressMode::ClampToEdge, {}, 9});
    if (bindlessSupported) {
        constexpr std::array<std::uint8_t, 4> white{255, 255, 255, 255};
        constexpr std::array<std::uint8_t, 4> normal{128, 128, 255, 255};
        bindlessFallbackBase = device.uploadTexture(
            {{1, 1}, rhi::Format::Rgba8Srgb, rhi::TextureUsage::CopyDestination | rhi::TextureUsage::Sampled, 1},
            std::as_bytes(std::span(white)));
        bindlessFallbackNormal = device.uploadTexture(
            {{1, 1}, rhi::Format::Rgba8Unorm, rhi::TextureUsage::CopyDestination | rhi::TextureUsage::Sampled, 1},
            std::as_bytes(std::span(normal)));
    }
    uniformBuffer = device.createBuffer({sizeof(SceneUniform), rhi::BufferUsage::Uniform, rhi::MemoryUsage::Upload});
    if (uniformBuffer->mapped().size() < sizeof(SceneUniform))
        throw std::runtime_error("Scene uniform buffer is not host writable");
    const std::array writes{rhi::BindingWrite{0, uniformBuffer.get(), nullptr, nullptr}};
    sceneDescriptor = device.createBindingSet(*sceneLayout, writes);
    // The scene shader always declares set 2, even while shadows are disabled.
    // Upload a real, initialized white image so every draw has a valid descriptor.
    constexpr std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    fallbackShadowTexture = device.uploadTexture(
        {{1, 1}, rhi::Format::Rgba8Unorm, rhi::TextureUsage::CopyDestination | rhi::TextureUsage::Sampled, 1},
        std::as_bytes(std::span(white)));
    std::array<rhi::BindingWrite, kMaxShadowCascades> shadowWrites;
    for (std::uint32_t i = 0; i < kMaxShadowCascades; ++i)
        shadowWrites[i] = {i, nullptr, fallbackShadowTexture.get(), pointSampler.get()};
    fallbackShadowDescriptor = device.createBindingSet(*shadowSetLayout, shadowWrites);
    constexpr std::array<std::uint8_t, 4> black{0, 0, 0, 255};
    constexpr std::array<std::uint8_t, 24> blackFaces{0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255,
                                                      0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    constexpr auto fallbackUsage = rhi::TextureUsage::CopyDestination | rhi::TextureUsage::Sampled;
    fallbackEnvironmentCube =
        device.uploadTexture({{1, 1}, rhi::Format::Rgba8Unorm, fallbackUsage, 1, 1, rhi::TextureDimension::Cube},
                             std::as_bytes(std::span(blackFaces)));
    fallbackEnvironmentLut =
        device.uploadTexture({{1, 1}, rhi::Format::Rgba8Unorm, fallbackUsage, 1}, std::as_bytes(std::span(black)));
    const std::array environmentWrites{
        rhi::BindingWrite{0, nullptr, fallbackEnvironmentCube.get(), environmentSampler.get()},
        rhi::BindingWrite{1, nullptr, fallbackEnvironmentCube.get(), environmentSampler.get()},
        rhi::BindingWrite{2, nullptr, fallbackEnvironmentLut.get(), environmentSampler.get()}};
    fallbackEnvironmentDescriptor = device.createBindingSet(*environmentSetLayout, environmentWrites);
    pipelineLayout = device.createPipelineLayout(
        {{sceneLayout.get(), materialLayout.get(), shadowSetLayout.get(), environmentSetLayout.get()},
         sizeof(MaterialPush),
         Fragment});
    if (bindlessSupported) {
        bindlessPipelineLayout = device.createPipelineLayout(
            {{sceneLayout.get(), bindlessMaterialLayout.get(), shadowSetLayout.get(), environmentSetLayout.get()},
             sizeof(BindlessMaterialPush),
             Fragment});
        cullPipelineLayout = device.createPipelineLayout({{sceneLayout.get(), cullLayout.get()}, sizeof(GpuCullPush),
                                                           Compute});
        auto shader = device.createShader({rhi::ShaderStage::Compute, rhi::ShaderFormat::Spirv,
                                           detail::embeddedShader(detail::ShaderId::CullCompute)});
        cullPipeline = device.createComputePipeline({cullPipelineLayout.get(), shader.get()});
    }
    shadowPipelineLayout =
        device.createPipelineLayout({{sceneLayout.get(), materialLayout.get()}, sizeof(ShadowPush), Vertex | Fragment});
    postLayout = device.createPipelineLayout({{postSetLayout.get()}, sizeof(aeg::math::Vec2), Fragment});
    if (temporalSupported) {
        temporalSetLayout = device.createBindingLayout({{{0, CombinedTextureSampler, Fragment},
                                                         {1, CombinedTextureSampler, Fragment},
                                                         {2, CombinedTextureSampler, Fragment},
                                                         {3, CombinedTextureSampler, Fragment}}});
        temporalLayout = device.createPipelineLayout({{temporalSetLayout.get()}, sizeof(TemporalPush), Fragment});
    }
}

void Renderer::Impl::initializeEnvironmentSupport() {
    if (!EnvironmentBaker::supported(device)) return;
    const auto& caps = device.capabilities();
    constexpr auto usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySource;
    const auto cube = device.textureSupport(rhi::Format::Rgba32Float, usage, rhi::TextureDimension::Cube);
    const auto flat = device.textureSupport(rhi::Format::Rgba32Float, usage);
    const auto source = device.textureSupport(rhi::Format::Rgba32Float,
                                              rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDestination);
    const auto defaults = EnvironmentBakeSettings{};
    const auto maximumCube =
        std::min({caps.maximumCubeImageDimension, cube.maximumExtent.width, cube.maximumExtent.height});
    const auto maximumFlat =
        std::min({caps.maximumImageDimension, flat.maximumExtent.width, flat.maximumExtent.height});
    const auto maximumSize =
        std::max({defaults.environmentSize, defaults.irradianceSize, defaults.prefilterSize, defaults.brdfSize});
    // First enable uploads makeProceduralEnvironment()'s fixed 512x256 panorama.
    environmentSupported = defaults.environmentSize <= maximumCube && defaults.brdfSize <= maximumFlat &&
                           defaults.irradianceSize <= maximumCube && defaults.prefilterSize <= maximumCube &&
                           (maximumSize + 7) / 8 <= caps.maximumComputeGroupCount[0] &&
                           (maximumSize + 7) / 8 <= caps.maximumComputeGroupCount[1] && source.supported &&
                           source.linearFiltering && (source.sampleMask & 1u) && caps.maximumImageDimension >= 512 &&
                           source.maximumExtent.width >= 512 && source.maximumExtent.height >= 256;
}

void Renderer::Impl::uploadEnvironment(const assets::HdrImageData& image) {
    if (!environmentSupported) throw std::runtime_error("Compute environment lighting is unsupported");
    // Keep both the first immutable LUT and every replacement map private until all work succeeds.
    std::unique_ptr<EnvironmentBaker> candidateBaker;
    auto* baker = environmentBaker.get();
    if (!baker) {
        candidateBaker = std::make_unique<EnvironmentBaker>(device);
        baker = candidateBaker.get();
    }
    auto candidate = std::make_unique<EnvironmentResources>();
    candidate->maps = baker->bake(image);
    const std::array writes{rhi::BindingWrite{0, nullptr, candidate->maps.irradiance.get(), environmentSampler.get()},
                            rhi::BindingWrite{1, nullptr, candidate->maps.prefiltered.get(), environmentSampler.get()},
                            rhi::BindingWrite{2, nullptr, &baker->brdfLut(), environmentSampler.get()}};
    candidate->descriptor = device.createBindingSet(*environmentSetLayout, writes);
    device.waitIdle();
    if (candidateBaker) environmentBaker.swap(candidateBaker);
    environmentResources.swap(candidate);
    resetTemporalHistory();
}

void Renderer::Impl::setEnvironment(const EnvironmentSettings& requested) {
    if (!std::isfinite(requested.intensity) || requested.intensity < 0 || requested.intensity > 100 ||
        !std::isfinite(requested.rotationRadians))
        throw std::invalid_argument("Environment intensity must be in [0,100] and rotation must be finite");
    if (requested == environmentSettings) return;
    if (requested.enabled && !environmentSupported)
        throw std::runtime_error("Compute environment lighting is unsupported");
    if (requested.enabled && !environmentResources) uploadEnvironment(assets::makeProceduralEnvironment());
    environmentSettings = requested;
    resetTemporalHistory();
}

void Renderer::Impl::invalidateOutput() noexcept {
    pendingFrame = false;
    pendingOutput.reset();
    submittedOutput.reset();
}

void Renderer::Impl::prepareRenderGraph(rhi::CommandList& commands) {
    (void)commands;
}

void Renderer::Impl::recordFxaaGraph(rhi::CommandList& commands) {
    (void)commands;
}

std::shared_ptr<rhi::Texture> Renderer::Impl::outputTexture() const {
    if (pendingFrame) return pendingOutput;
    if (submittedOutput) return submittedOutput;
    return viewport->filtered ? viewport->filtered : viewport->toneMapped;
}

void Renderer::Impl::recordFrame(rhi::CommandList& commands, const RenderSettings& settings) {
    retryingFrame = pendingFrame;
    if (graphIntermediate) {
        // EditorGraphics has waited for the prior frame fence before recording
        // a replacement. CPU-only retries retain the pending lease and reuse
        // that same physical image instead of changing output identity.
        graphPool.reclaimCompletedFrame(graphFrame + 1);
        if (!retryingFrame) graphIntermediate = nullptr;
    }
    pendingFrame = false;
    if (!retryingFrame) pendingOutput.reset();
    graphFiltered.reset();
    graphPostDescriptor.reset();
    // Recording is speculative. Acquisition/submit failure must leave the
    // previous frame's jitter, history and capture image unchanged.
    recordingHistory = temporalHistory;
    if (aa.taa) {
        pendingHistoryColorState = viewport->historyColorState;
        pendingHistoryDepthState = viewport->historyDepthState;
    }
    auto uniform = sceneUniform(settings);
    configureShadowUniform(uniform, settings);
    std::memcpy(uniformBuffer->mapped().data(), &uniform, sizeof(uniform));

    // Create all lazy scene variants before graph execution starts. Graph
    // callbacks are draw bodies only and therefore run inside an active pass.
    auto& scene = scenePipeline(aa.msaaSamples, aa.taa, settings.clipSpace.reversed_z);
    const bool hasTransparent = model && std::ranges::any_of(model->materials, [](const auto& material) {
        return material.parameters.alphaMode == static_cast<std::uint32_t>(assets::AlphaMode::Blend);
    });
    if (hasTransparent) scenePipeline(aa.msaaSamples, aa.taa, settings.clipSpace.reversed_z, true);

    RenderGraph graph;
    std::vector<RenderGraph::Handle> shadowHandles;
    if (uniform.shadowInfo.x >= 0.5f && shadow && model) {
        const auto count = static_cast<std::uint32_t>(uniform.shadowInfo.w);
        shadowHandles.reserve(count);
        for (std::uint32_t cascade = 0; cascade < count; ++cascade) {
            const auto handle = graph.importTexture(*shadow->textures[cascade], shadow->states[cascade],
                                                    "shadow-" + std::to_string(cascade));
            shadowHandles.push_back(handle);
            auto pass = graph.addPass(
                "Shadow cascade " + std::to_string(cascade), RenderGraph::PassType::Graphics,
                RenderGraph::PassFlags::None,
                [this, &uniform, &commands, cascade](RenderGraph::PassContext&) { drawShadow(commands, uniform, cascade); });
            pass.write(handle, RenderGraph::Usage::DepthWrite)
                .clear(rhi::ClearValue{})
                .setFinalState(handle, rhi::ResourceState::Sampled)
                .setExternalRenderPass(*shadow->pass, *shadow->framebuffers[cascade],
                                       {shadow->resolution, shadow->resolution});
        }
    }

    const auto color = graph.importTexture(*viewport->color, viewport->colorState, "scene-color");
    const auto depth = graph.importTexture(*viewport->depth, viewport->depthState, "scene-depth");
    std::optional<RenderGraph::Handle> multisample;
    if (viewport->multisample)
        multisample = graph.importTexture(*viewport->multisample, viewport->multisampleState, "scene-msaa");
    std::optional<RenderGraph::Handle> motion;
    if (viewport->motion)
        motion = graph.importTexture(*viewport->motion, viewport->motionState, "scene-motion");

    std::array<rhi::ClearValue, 3> sceneClears{};
    const auto finiteRadiance = [](float value) {
        return std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, 65504.0f);
    };
    sceneClears[0].color = {finiteRadiance(settings.background.x), finiteRadiance(settings.background.y),
                            finiteRadiance(settings.background.z), 1};
    sceneClears[1].depth = settings.clipSpace.reversed_z ? 0.0f : 1.0f;
    sceneClears[2].color = {0, 0, -1, -1};
    auto scenePass = graph.addPass("Scene", RenderGraph::PassType::Graphics, RenderGraph::PassFlags::None,
                                   [this, &settings, &uniform, &commands](RenderGraph::PassContext&) {
                                       drawScene(commands, settings, uniform);
                                   });
    scenePass.write(color, RenderGraph::Usage::ColorWrite).write(depth, RenderGraph::Usage::DepthWrite);
    if (multisample) scenePass.write(*multisample, RenderGraph::Usage::ColorWrite);
    if (motion) scenePass.write(*motion, RenderGraph::Usage::ColorWrite);
    for (const auto handle : shadowHandles) scenePass.read(handle, RenderGraph::Usage::SampledRead);
    const auto clearCount = aa.taa ? 3u : 2u;
    for (std::uint32_t index = 0; index < clearCount; ++index) scenePass.clear(sceneClears[index]);
    scenePass.setExternalRenderPass(*scene.pass, *viewport->sceneFrame, viewport->extent());

    RenderGraph::Handle toneSource = color;
    const auto readIndex = recordingHistory.readIndex;
    const auto writeIndex = 1u - readIndex;
    std::optional<RenderGraph::Handle> historyColorWrite;
    std::optional<RenderGraph::Handle> historyDepthWrite;
    if (aa.taa) {
        const auto historyColorRead = graph.importTexture(*viewport->historyColor[readIndex],
                                                          pendingHistoryColorState[readIndex], "taa-history-color-read");
        const auto historyDepthRead = graph.importTexture(*viewport->historyDepth[readIndex],
                                                          pendingHistoryDepthState[readIndex], "taa-history-depth-read");
        historyColorWrite = graph.importTexture(*viewport->historyColor[writeIndex],
                                                 pendingHistoryColorState[writeIndex], "taa-history-color-write");
        historyDepthWrite = graph.importTexture(*viewport->historyDepth[writeIndex],
                                                 pendingHistoryDepthState[writeIndex], "taa-history-depth-write");
        auto taaPass = graph.addPass("TAA", RenderGraph::PassType::Graphics, RenderGraph::PassFlags::None,
                                     [this, &commands](RenderGraph::PassContext&) { drawTemporal(commands); });
        taaPass.read(color, RenderGraph::Usage::SampledRead);
        taaPass.read(*motion, RenderGraph::Usage::SampledRead);
        taaPass.read(historyColorRead, RenderGraph::Usage::SampledRead);
        taaPass.read(historyDepthRead, RenderGraph::Usage::SampledRead);
        taaPass.write(*historyColorWrite, RenderGraph::Usage::ColorWrite)
            .write(*historyDepthWrite, RenderGraph::Usage::ColorWrite)
            .setFinalState(*historyColorWrite, rhi::ResourceState::Sampled)
            .setFinalState(*historyDepthWrite, rhi::ResourceState::Sampled)
            .setExternalRenderPass(*temporalPass, *viewport->temporalFrames[writeIndex], viewport->extent());
        toneSource = *historyColorWrite;
    }

    const auto toneMapped = graph.importTexture(*viewport->toneMapped, viewport->toneMappedState, "tone-mapped");
    auto toneGraphPass = graph.addPass("Tone Mapping", RenderGraph::PassType::Graphics, RenderGraph::PassFlags::None,
                                       [this, &commands](RenderGraph::PassContext&) { drawToneMapping(commands); });
    toneGraphPass.read(toneSource, RenderGraph::Usage::SampledRead)
        .write(toneMapped, RenderGraph::Usage::ColorWrite)
        .setFinalState(toneMapped, rhi::ResourceState::Sampled)
        .setExternalRenderPass(*this->tonePass, *viewport->toneFrame, viewport->extent());

    RenderGraph::Handle output = toneMapped;
    if (aa.fxaa) {
        const auto intermediate = graph.createTexture(
            {{viewport->width, viewport->height}, displayFormat, outputUsage}, "fxaa-intermediate", true);
        auto fxaaPass = graph.addPass("FXAA", RenderGraph::PassType::Graphics, RenderGraph::PassFlags::None,
                                      [this, intermediate, &commands](RenderGraph::PassContext& context) {
                                          auto& texture = context.texture(intermediate);
                                          graphIntermediate = &texture;
                                          graphFiltered = graphPool.lease(texture);
                                          const std::array writes{rhi::BindingWrite{0, nullptr, viewport->toneMapped.get(),
                                                                                   viewportSampler.get()}};
                                          graphPostDescriptor = device.createBindingSet(*postSetLayout, writes);
                                          commands.bindPipeline(*postPipeline);
                                          commands.bindSet(*postLayout, 0, *graphPostDescriptor);
                                          commands.setViewport(viewport->extent());
                                          const aeg::math::Vec2 reciprocal{1.0f / static_cast<float>(viewport->width),
                                                                           1.0f / static_cast<float>(viewport->height)};
                                          commands.pushConstants(*postLayout, std::as_bytes(std::span(&reciprocal, 1)));
                                          commands.draw(3);
                                      });
        fxaaPass.read(toneMapped, RenderGraph::Usage::SampledRead)
            .write(intermediate, RenderGraph::Usage::ColorWrite)
            .setFinalState(intermediate, rhi::ResourceState::Sampled)
            .setExternalRenderPass(*postPass, viewport->extent(), &graphPostFrame);
        output = intermediate;
    }

    graph.compile({output});
    graphDump = graph.debugDump();
    prepareGpuCulling(commands, settings, uniform);
    graph.execute(commands, device, graphPool, ++graphFrame);
    if (aa.fxaa && !retryingFrame) {
        viewport->filtered = std::move(graphFiltered);
    }
    if (!retryingFrame) pendingOutput = aa.fxaa ? viewport->filtered : viewport->toneMapped;
    // std::cerr << "pending=" << pendingOutput.get() << " retry=" << retryingFrame << "\n";
    pendingSettings = settings;
    pendingFrame = true;
}

void Renderer::Impl::frameSubmitted() {
    if (!pendingFrame) return;
    submittedOutput = pendingOutput;
    if (aa.taa) {
        recordingHistory.readIndex = 1u - recordingHistory.readIndex;
        recordingHistory.previous = pendingSettings;
        recordingHistory.valid = true;
        if (recordingHistory.frames != std::numeric_limits<std::uint32_t>::max()) ++recordingHistory.frames;
        recordingHistory.jitterIndex = (recordingHistory.jitterIndex + 1) % 8;
        temporalHistory = recordingHistory;
        viewport->historyInitialized = true;
        viewport->historyColorState = pendingHistoryColorState;
        viewport->historyDepthState = pendingHistoryDepthState;
    }
    pendingFrame = false;
    pendingOutput.reset();
}

Renderer::Renderer(rhi::Device& device) : impl_(std::make_unique<Impl>(device)) {}
Renderer::~Renderer() = default;
void Renderer::uploadModel(const ModelData& model) { impl_->uploadModel(model); }
void Renderer::uploadEnvironment(const assets::HdrImageData& image) { impl_->uploadEnvironment(image); }
void Renderer::useProceduralEnvironment() { uploadEnvironment(assets::makeProceduralEnvironment()); }
void Renderer::setEnvironment(const EnvironmentSettings& settings) { impl_->setEnvironment(settings); }
EnvironmentSettings Renderer::environment() const { return impl_->environmentSettings; }
bool Renderer::supportsEnvironment() const { return impl_->environmentSupported; }
bool Renderer::hasEnvironment() const { return bool(impl_->environmentResources); }
void Renderer::setToneMapping(const ToneMappingSettings& settings) { impl_->setToneMapping(settings); }
ToneMappingSettings Renderer::toneMapping() const { return impl_->toneMappingSettings; }
void Renderer::setAntialiasing(const AntialiasingSettings& settings) { impl_->setAntialiasing(settings); }
AntialiasingSettings Renderer::antialiasing() const { return impl_->aa; }
std::uint32_t Renderer::supportedMsaaSamples() const { return impl_->sampleMask; }
bool Renderer::supportsTemporalAA() const { return impl_->temporalSupported; }
void Renderer::setShadows(const ShadowSettings& settings) { impl_->setShadows(settings); }
ShadowSettings Renderer::shadows() const { return impl_->shadowSettings; }
bool Renderer::supportsShadows() const { return impl_->shadowSupported; }
std::uint32_t Renderer::maximumShadowResolution() const { return impl_->maximumShadow; }
void Renderer::resetTemporalHistory() { impl_->resetTemporalHistory(); }
std::uint32_t Renderer::temporalFrameCount() const { return impl_->temporalHistory.frames; }
void Renderer::resizeViewport(std::uint32_t width, std::uint32_t height) { impl_->resizeViewport(width, height); }
std::shared_ptr<rhi::Texture> Renderer::outputTexture() const { return impl_->outputTexture(); }
void Renderer::recordFrame(rhi::CommandList& commands, const RenderSettings& settings) {
    impl_->recordFrame(commands, settings);
}
void Renderer::frameSubmitted() { impl_->frameSubmitted(); }
void Renderer::captureViewport(const std::filesystem::path& output) { impl_->captureViewport(output); }
void Renderer::waitIdle() { impl_->device.waitIdle(); }
const std::string& Renderer::deviceName() const { return impl_->device.deviceName(); }
std::uint32_t Renderer::validationErrorCount() const { return impl_->device.validationErrorCount(); }
const std::string& Renderer::renderGraphDump() const { return impl_->graphDump; }
}
