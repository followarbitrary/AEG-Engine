#pragma once
#include <RhiPipeline.hpp>
#include <array>
#include <memory>
namespace aeg::render {
struct ViewportTargets {
    std::uint32_t width{}, height{};
    bool historyInitialized{};
    std::shared_ptr<rhi::Texture> color, depth, multisample, filtered, motion, toneMapped;
    std::array<std::shared_ptr<rhi::Texture>, 2> historyColor, historyDepth;
    // Imported TAA images retain their state between graph executions. The
    // graph owns the transitions while the viewport retains the textures.
    std::array<rhi::ResourceState, 2> historyColorState{}, historyDepthState{};
    std::unique_ptr<rhi::BindingSet> postDescriptor;
    std::array<std::unique_ptr<rhi::BindingSet>, 2> temporalDescriptors;
    std::array<std::unique_ptr<rhi::BindingSet>, 3> toneDescriptors;
    std::unique_ptr<rhi::Framebuffer> sceneFrame, postFrame, toneFrame;
    std::array<std::unique_ptr<rhi::Framebuffer>, 2> temporalFrames;
    [[nodiscard]] rhi::Extent2D extent() const { return {width, height}; }
};
}
