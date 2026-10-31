#pragma once
#include <RhiCommandList.hpp>
#include <imgui.h>
#include <cstdint>
namespace aeg::gui {
class GuiRenderer {
public:
    virtual ~GuiRenderer() = default;
    // Optional notification for backends that retire resources by frame slot.
    // The default keeps lightweight and legacy implementations source-compatible.
    virtual void beginFrame(std::uint32_t slot, std::uint32_t slotCount) {
        (void)slot;
        (void)slotCount;
    }
    virtual void newFrame() = 0;
    virtual ImTextureID registerTexture(rhi::Texture& texture, rhi::Sampler& sampler) = 0;
    virtual void updateTexture(ImTextureID id, rhi::Texture& texture, rhi::Sampler& sampler) = 0;
    // Returns the descriptor to use for this frame. Backends that cannot update
    // descriptors in place may return a fresh descriptor while retaining the
    // source-compatible updateTexture API above.
    virtual ImTextureID updateTextureForFrame(ImTextureID id, rhi::Texture& texture, rhi::Sampler& sampler) {
        updateTexture(id, texture, sampler);
        return id;
    }
    virtual void unregisterTexture(ImTextureID id) = 0;
    // Caller has begun the swapchain render pass.
    virtual void renderDrawData(rhi::CommandList& commands, ImDrawData* data) = 0;
};
}
