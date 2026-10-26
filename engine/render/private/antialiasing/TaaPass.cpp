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
    if (!viewport->historyInitialized) {
        // The first temporal pass binds both history images, even when it
        // ignores their contents. Only a successful submit initializes them.
        for (std::size_t i = 0; i < 2; ++i)
            for (const auto& image : {viewport->historyColor[i], viewport->historyDepth[i]})
                commands.transition(*image, rhi::ResourceState::Undefined, rhi::ResourceState::Sampled);
    }
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
}

void Renderer::Impl::pointFinalDescriptorAtTemporalOutput() {
    if (!aa.taa || !aa.fxaa) return;
    const std::array writes{rhi::BindingWrite{0, nullptr, viewport->historyColor[1u - recordingHistory.readIndex].get(),
                                              viewportSampler.get()}};
    viewport->postDescriptor->update(writes);
}
}
