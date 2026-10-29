#include "RendererImpl.hpp"

namespace aeg::render {
void Renderer::Impl::resetTemporalHistory() noexcept {
    temporalHistory = TemporalHistory{};
    pendingFrame = false;
    pendingOutput.reset();
}

bool Renderer::Impl::perspectiveProjection(const aeg::math::Mat4& matrix) {
    return aeg::math::dot(aeg::math::Vec3(matrix[0][3], matrix[1][3], matrix[2][3]),
                          aeg::math::Vec3(matrix[0][3], matrix[1][3], matrix[2][3])) > 1e-12f;
}

void Renderer::Impl::drawTemporal(rhi::CommandList& commands) {
    if (!aa.taa) return;
    // TAA history images are imported into the renderer graph at frame start;
    // its sampled-read declarations own these transitions for both first use
    // and subsequent ping-pong frames.
    const auto writeIndex = 1u - recordingHistory.readIndex;
    commands.beginRenderPass(*temporalPass, *viewport->temporalFrames[writeIndex], viewport->extent(), {});
    commands.setViewport(viewport->extent());
    commands.bindPipeline(*temporalPipeline);
    commands.bindSet(*temporalLayout, 0, *viewport->temporalDescriptors[recordingHistory.readIndex]);
    const TemporalPush push{
        {1.0f / static_cast<float>(viewport->width), 1.0f / static_cast<float>(viewport->height)},
        std::min(0.9f, static_cast<float>(recordingHistory.frames) / (static_cast<float>(recordingHistory.frames) + 1)),
        recordingHistory.valid ? 1u : 0u};
    commands.pushConstants(*temporalLayout, std::as_bytes(std::span(&push, 1)));
    commands.draw(3);
    commands.endRenderPass();
    // The temporal render pass owns the write image's attachment transition;
    // publish its resulting sampled state only after frameSubmitted().
    pendingHistoryColorState[writeIndex] = rhi::ResourceState::Sampled;
    pendingHistoryDepthState[writeIndex] = rhi::ResourceState::Sampled;
}

}
