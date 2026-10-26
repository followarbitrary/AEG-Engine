#pragma once
#include <RhiPipeline.hpp>
#include <span>
namespace aeg::rhi {
class CommandList {
public:
    virtual ~CommandList() = default;
    virtual void beginRenderPass(RenderPass& pass, Framebuffer& framebuffer, Extent2D extent,
                                 std::span<const ClearValue> clears) = 0;
    virtual void endRenderPass() = 0;
    virtual void setViewport(Extent2D extent) = 0;
    virtual void bindPipeline(GraphicsPipeline& pipeline) = 0;
    virtual void bindComputePipeline(ComputePipeline& pipeline) = 0;
    virtual void bindSet(PipelineLayout& layout, std::uint32_t index, BindingSet& set,
                         PipelineBindPoint point = PipelineBindPoint::Graphics) = 0;
    virtual void bindVertexBuffer(Buffer& buffer) = 0;
    virtual void bindIndexBuffer(Buffer& buffer) = 0;
    virtual void pushConstants(PipelineLayout& layout, std::span<const std::byte> bytes) = 0;
    virtual void draw(std::uint32_t vertices) = 0;
    virtual void drawIndexed(std::uint32_t indices) = 0;
    virtual void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;
    virtual void transition(Texture& texture, ResourceState before, ResourceState after) = 0;
    virtual void copyBuffer(Buffer& source, Buffer& destination, std::size_t bytes) = 0;
    virtual void copyTextureToBuffer(Texture& source, Buffer& destination, TextureSubresource subresource = {}) = 0;
};
}
