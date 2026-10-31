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
    if (!recordingTaa) return;
    // History image transitions and attachment barriers are owned by the
    // renderer graph; this callback only records the resolve draw body.
    commands.setViewport(viewport->extent());
    commands.bindPipeline(*temporalPipeline);
    commands.bindSet(*temporalLayout, 0, *viewport->temporalDescriptors[recordingHistory.readIndex]);
    const TemporalPush push{
        {1.0f / static_cast<float>(viewport->width), 1.0f / static_cast<float>(viewport->height)},
        std::min(aa.taaHistoryWeight,
                 static_cast<float>(recordingHistory.frames) / (static_cast<float>(recordingHistory.frames) + 1)),
        aa.taaDepthThreshold,
        recordingHistory.valid ? 1u : 0u};
    commands.pushConstants(*temporalLayout, std::as_bytes(std::span(&push, 1)));
    commands.draw(3);
}

}
