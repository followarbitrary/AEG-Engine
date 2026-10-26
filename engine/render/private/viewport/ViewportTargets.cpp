#include "RendererImpl.hpp"

namespace aeg::render {
std::unique_ptr<ViewportTargets> Renderer::Impl::createViewport(std::uint32_t width, std::uint32_t height,
                                                                const AntialiasingSettings& settings) {
    auto& scene = scenePipeline(settings.msaaSamples, settings.taa);
    auto candidate = std::make_unique<ViewportTargets>();
    candidate->width = width;
    candidate->height = height;
    const auto create = [&](rhi::Format format, rhi::TextureUsage usage, std::uint32_t samples = 1) {
        return std::shared_ptr<rhi::Texture>(device.createTexture({{width, height}, format, usage, samples}));
    };
    candidate->color = create(sceneFormat, outputUsage);
    candidate->depth = create(device.capabilities().depthFormat, rhi::TextureUsage::DepthAttachment, scene.samples);
    const bool multisampled = settings.msaaSamples > 1;
    if (multisampled) candidate->multisample = create(sceneFormat, rhi::TextureUsage::ColorAttachment, scene.samples);
    if (settings.taa) candidate->motion = create(motionFormat, outputUsage);
    rhi::FramebufferDesc frame{
        scene.pass.get(),
        {multisampled ? candidate->multisample.get() : candidate->color.get(), candidate->depth.get()},
        {width, height}};
    if (multisampled || settings.taa)
        frame.attachments.push_back(settings.taa ? candidate->motion.get() : candidate->color.get());
    candidate->sceneFrame = device.createFramebuffer(frame);
    if (settings.fxaa) {
        candidate->filtered = create(sceneFormat, outputUsage);
        candidate->postFrame = device.createFramebuffer({postPass.get(), {candidate->filtered.get()}, {width, height}});
        const std::array writes{rhi::BindingWrite{0, nullptr, candidate->color.get(), viewportSampler.get()}};
        candidate->postDescriptor = device.createBindingSet(*postSetLayout, writes);
    }
    if (settings.taa) {
        for (std::size_t i = 0; i < 2; ++i) {
            candidate->historyColor[i] = create(historyFormat, outputUsage);
            candidate->historyDepth[i] = create(historyDepthFormat, outputUsage);
            candidate->temporalFrames[i] =
                device.createFramebuffer({temporalPass.get(),
                                          {candidate->historyColor[i].get(), candidate->historyDepth[i].get()},
                                          {width, height}});
            const std::array writes{
                rhi::BindingWrite{0, nullptr, candidate->color.get(), pointSampler.get()},
                rhi::BindingWrite{1, nullptr, candidate->motion.get(), pointSampler.get()},
                rhi::BindingWrite{2, nullptr, candidate->historyColor[i].get(), viewportSampler.get()},
                rhi::BindingWrite{3, nullptr, candidate->historyDepth[i].get(), pointSampler.get()}};
            candidate->temporalDescriptors[i] = device.createBindingSet(*temporalSetLayout, writes);
        }
    }
    return candidate;
}

void Renderer::Impl::resizeViewport(std::uint32_t width, std::uint32_t height) {
    const auto maximum = aa.taa ? maximumTemporalViewport : maximumViewport;
    width = std::clamp(width, 1u, maximum);
    height = std::clamp(height, 1u, maximum);
    if (viewport && width == viewport->width && height == viewport->height) return;
    device.waitIdle();
    auto replacement = createViewport(width, height, aa);
    viewport.swap(replacement);
    invalidateOutput();
    resetTemporalHistory();
}
}
