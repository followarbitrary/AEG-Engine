#include "RendererImpl.hpp"

namespace aeg::render {
void Renderer::Impl::drawFxaa(rhi::CommandList& commands) {
    if (!aa.fxaa) return;
    commands.beginRenderPass(*postPass, *viewport->postFrame, viewport->extent(), {});
    commands.setViewport(viewport->extent());
    commands.bindPipeline(*postPipeline);
    commands.bindSet(*postLayout, 0, *viewport->postDescriptor);
    const aeg::math::Vec2 reciprocal{1.0f / static_cast<float>(viewport->width), 1.0f / static_cast<float>(viewport->height)};
    commands.pushConstants(*postLayout, std::as_bytes(std::span(&reciprocal, 1)));
    commands.draw(3);
    commands.endRenderPass();
}
}
