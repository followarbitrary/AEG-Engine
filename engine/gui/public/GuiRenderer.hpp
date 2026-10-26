#pragma once
#include <RhiCommandList.hpp>
#include <imgui.h>
namespace aeg::gui {
class GuiRenderer {
public:
    virtual ~GuiRenderer() = default;
    virtual void newFrame() = 0;
    virtual ImTextureID registerTexture(rhi::Texture& texture, rhi::Sampler& sampler) = 0;
    virtual void updateTexture(ImTextureID id, rhi::Texture& texture, rhi::Sampler& sampler) = 0;
    virtual void unregisterTexture(ImTextureID id) = 0;
    // Caller has begun the swapchain render pass.
    virtual void renderDrawData(rhi::CommandList& commands, ImDrawData* data) = 0;
};
}
