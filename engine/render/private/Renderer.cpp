#include "RendererImpl.hpp"

namespace aeg::render {
Renderer::Impl::Impl(rhi::Device& target) : device(target) { initialize(); }

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
    const auto color = device.textureSupport(sceneFormat, rhi::TextureUsage::ColorAttachment);
    const auto single = device.textureSupport(sceneFormat, outputUsage);
    const auto depth = device.textureSupport(caps.depthFormat, rhi::TextureUsage::DepthAttachment);
    sampleMask = color.sampleMask & depth.sampleMask & 15u;
    if (!rhi::isDepthFormat(caps.depthFormat) || !color.supported || !single.supported || !depth.supported ||
        !(sampleMask & 1u) || !(single.sampleMask & 1u))
        throw std::runtime_error("Scene attachments do not support single sampling");
    if (!single.linearFiltering) throw std::runtime_error("Scene format does not support linear sampling");
    if (!color.colorBlending || !single.colorBlending)
        throw std::runtime_error("Scene format does not support material alpha blending");
    maximumViewport = std::min({caps.maximumImageDimension, caps.maximumFramebufferDimension, color.maximumExtent.width,
                                color.maximumExtent.height, single.maximumExtent.width, single.maximumExtent.height,
                                depth.maximumExtent.width, depth.maximumExtent.height, 8192u});
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
    sceneLayout = device.createBindingLayout({{{0, UniformBuffer, Vertex | Fragment}}});
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
    sampler = device.createSampler({rhi::Filter::Linear, rhi::AddressMode::Repeat});
    viewportSampler = device.createSampler({rhi::Filter::Linear, rhi::AddressMode::ClampToEdge});
    pointSampler = device.createSampler({rhi::Filter::Nearest, rhi::AddressMode::ClampToEdge});
    environmentSampler = device.createSampler({rhi::Filter::Linear, rhi::AddressMode::ClampToEdge, {}, 9});
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

std::shared_ptr<rhi::Texture> Renderer::Impl::outputTexture() const {
    if (pendingFrame) return pendingOutput;
    if (submittedOutput) return submittedOutput;
    return viewport->filtered ? viewport->filtered : viewport->color;
}

void Renderer::Impl::recordFrame(rhi::CommandList& commands, const RenderSettings& settings) {
    pendingFrame = false;
    pendingOutput.reset();
    // Recording is speculative. Acquisition/submit failure must leave the
    // previous frame's jitter, history and capture image unchanged.
    recordingHistory = temporalHistory;
    auto uniform = sceneUniform(settings);
    configureShadowUniform(uniform, settings);
    std::memcpy(uniformBuffer->mapped().data(), &uniform, sizeof(uniform));
    drawShadow(commands, uniform);
    drawScene(commands, settings, uniform);
    pointFinalDescriptorAtTemporalOutput();
    drawTemporal(commands);
    drawFxaa(commands);
    pendingOutput = aa.fxaa  ? viewport->filtered
                    : aa.taa ? viewport->historyColor[1u - recordingHistory.readIndex]
                             : viewport->color;
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
}
