#pragma once
#include <RhiPipeline.hpp>
#include <ShadowSettings.hpp>
#include <array>
#include <memory>

namespace aeg::render {
struct ShadowResources {
    std::uint32_t resolution{};
    std::uint32_t capacity{};
    // Reverse destruction releases descriptors, pipeline and framebuffer before
    // their image, pass, and the renderer-owned sampler/layout dependencies.
    std::unique_ptr<rhi::RenderPass> pass;
    std::array<std::unique_ptr<rhi::Texture>, kMaxShadowCascades> textures;
    std::array<std::unique_ptr<rhi::Framebuffer>, kMaxShadowCascades> framebuffers;
    std::unique_ptr<rhi::GraphicsPipeline> pipeline;
    // Variant i exposes maps 0..i and initialized fallback textures thereafter.
    std::array<std::unique_ptr<rhi::BindingSet>, kMaxShadowCascades> descriptors;
};
}
