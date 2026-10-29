#include "RendererImpl.hpp"

namespace aeg::render {
void Renderer::Impl::drawFxaa(rhi::CommandList& commands) {
    if (!aa.fxaa) return;
    auto* frame = graphPostFrame ? graphPostFrame.get() : viewport->postFrame.get();
    auto* descriptor = graphPostDescriptor ? graphPostDescriptor.get() : viewport->postDescriptor.get();
    if (!frame || !descriptor)
        throw std::logic_error("FXAA graph intermediate was not prepared");
    commands.beginRenderPass(*postPass, *frame, viewport->extent(), {});
    commands.setViewport(viewport->extent());
    commands.bindPipeline(*postPipeline);
    commands.bindSet(*postLayout, 0, *descriptor);
    const aeg::math::Vec2 reciprocal{1.0f / static_cast<float>(viewport->width),
                                     1.0f / static_cast<float>(viewport->height)};
    commands.pushConstants(*postLayout, std::as_bytes(std::span(&reciprocal, 1)));
    commands.draw(3);
    commands.endRenderPass();
}
}
