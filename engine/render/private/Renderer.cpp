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
    postSetLayout = device.createBindingLayout({{{0, CombinedTextureSampler, Fragment}}});
    sampler = device.createSampler({rhi::Filter::Linear, rhi::AddressMode::Repeat});
    viewportSampler = device.createSampler({rhi::Filter::Linear, rhi::AddressMode::ClampToEdge});
    pointSampler = device.createSampler({rhi::Filter::Nearest, rhi::AddressMode::ClampToEdge});
    uniformBuffer = device.createBuffer({sizeof(SceneUniform), rhi::BufferUsage::Uniform, rhi::MemoryUsage::Upload});
    if (uniformBuffer->mapped().size() < sizeof(SceneUniform))
        throw std::runtime_error("Scene uniform buffer is not host writable");
    const std::array writes{rhi::BindingWrite{0, uniformBuffer.get(), nullptr, nullptr}};
    sceneDescriptor = device.createBindingSet(*sceneLayout, writes);
    pipelineLayout =
        device.createPipelineLayout({{sceneLayout.get(), materialLayout.get()}, sizeof(MaterialPush), Fragment});
    postLayout = device.createPipelineLayout({{postSetLayout.get()}, sizeof(aeg::math::Vec2), Fragment});
    if (temporalSupported) {
        temporalSetLayout = device.createBindingLayout({{{0, CombinedTextureSampler, Fragment},
                                                         {1, CombinedTextureSampler, Fragment},
                                                         {2, CombinedTextureSampler, Fragment},
                                                         {3, CombinedTextureSampler, Fragment}}});
        temporalLayout = device.createPipelineLayout({{temporalSetLayout.get()}, sizeof(TemporalPush), Fragment});
    }
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
    drawScene(commands, settings);
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
void Renderer::setAntialiasing(const AntialiasingSettings& settings) { impl_->setAntialiasing(settings); }
AntialiasingSettings Renderer::antialiasing() const { return impl_->aa; }
std::uint32_t Renderer::supportedMsaaSamples() const { return impl_->sampleMask; }
bool Renderer::supportsTemporalAA() const { return impl_->temporalSupported; }
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
