#pragma once
#include <RhiPipeline.hpp>
#include <memory>

namespace aeg::render {
struct ShadowResources {
    std::uint32_t resolution{};
    // Reverse destruction releases descriptors, pipeline and framebuffer before
    // their image, pass, and the renderer-owned sampler/layout dependencies.
    std::unique_ptr<rhi::RenderPass> pass;
    std::unique_ptr<rhi::Texture> texture;
    std::unique_ptr<rhi::Framebuffer> framebuffer;
    std::unique_ptr<rhi::GraphicsPipeline> pipeline;
    std::unique_ptr<rhi::BindingSet> descriptor;
};
}
