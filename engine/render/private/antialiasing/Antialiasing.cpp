#include "RendererImpl.hpp"

namespace aeg::render {
void Renderer::Impl::createPostPass() {
    const rhi::AttachmentDesc color{displayFormat,
                                    1,
                                    rhi::LoadOp::Discard,
                                    rhi::StoreOp::Store,
                                    rhi::ResourceState::ColorAttachment,
                                    rhi::ResourceState::ColorAttachment};
    postPass = device.createRenderPass({{color}, {0}, {}, {}});
    if (temporalSupported) {
        auto history = color, depth = color;
        history.format = historyFormat;
        depth.format = historyDepthFormat;
        temporalPass = device.createRenderPass({{history, depth}, {0, 1}, {}, {}});
    }
}

void Renderer::Impl::setAntialiasing(const AntialiasingSettings& requested) {
    if (requested.msaaSamples != 1 && requested.msaaSamples != 2 && requested.msaaSamples != 4 &&
        requested.msaaSamples != 8)
        throw std::invalid_argument("MSAA sample count must be 1, 2, 4 or 8");
    if (requested.taa && !temporalSupported)
        throw std::runtime_error("TAA requires independent attachment blending and sampled RGBA32F motion, RGBA16F "
                                 "color and R32F depth attachments");
    if (!std::isfinite(requested.taaHistoryWeight) || requested.taaHistoryWeight < 0.0f ||
        requested.taaHistoryWeight > 0.9f)
        throw std::invalid_argument("TAA history weight must be finite and within [0, 0.9]");
    if (!std::isfinite(requested.taaDepthThreshold) || requested.taaDepthThreshold < 0.0f ||
        requested.taaDepthThreshold > 1.0f)
        throw std::invalid_argument("TAA depth threshold must be finite and within [0, 1]");
    AntialiasingSettings effective{1, requested.fxaa, requested.taa, requested.taaHistoryWeight,
                                   requested.taaDepthThreshold};
    if (!requested.taa)
        for (const auto samples : {8u, 4u, 2u, 1u})
            if (samples <= requested.msaaSamples && (sampleMask & samples)) {
                effective.msaaSamples = samples;
                break;
            }
    if (effective == aa) return;
    device.waitIdle();
    const auto maximum = effective.taa ? maximumTemporalViewport : maximumViewport;
    auto replacement =
        createViewport(std::min(viewport->width, maximum), std::min(viewport->height, maximum), effective);
    viewport.swap(replacement);
    aa = effective;
    invalidateOutput();
    resetTemporalHistory();
}
}
