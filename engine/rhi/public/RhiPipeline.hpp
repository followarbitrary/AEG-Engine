#pragma once
#include <RhiBinding.hpp>
#include <RhiShader.hpp>
#include <optional>
namespace aeg::rhi {
struct PipelineLayoutDesc {
    std::vector<BindingLayout*> sets;
    std::uint32_t pushConstantBytes{};
    ShaderStage pushConstantStages{ShaderStage::Fragment};
};
class PipelineLayout {
public:
    virtual ~PipelineLayout() = default;
};
struct AttachmentDesc {
    Format format{Format::Rgba8Unorm};
    std::uint32_t samples{1};
    LoadOp load{LoadOp::Discard};
    StoreOp store{StoreOp::Store};
    ResourceState initialState{ResourceState::Undefined}, finalState{ResourceState::Sampled};
};
struct RenderPassDesc {
    std::vector<AttachmentDesc> attachments;
    // Empty for a depth-only pass; at least one color or depth attachment is required.
    std::vector<std::uint32_t> colors;
    std::optional<std::uint32_t> depth;
    std::vector<std::uint32_t> resolves;
};
void validate(const RenderPassDesc& desc);
class RenderPass {
public:
    virtual ~RenderPass() = default;
};
struct FramebufferDesc {
    RenderPass* pass{};
    std::vector<Texture*> attachments;
    Extent2D extent;
};
class Framebuffer {
public:
    virtual ~Framebuffer() = default;
};
struct VertexAttribute {
    std::uint32_t location{}, offset{};
    VertexFormat format{VertexFormat::Float3};
};
enum class DepthCompare { Less, Greater };
enum class BlendMode { Opaque, Alpha };
struct GraphicsPipelineDesc {
    RenderPass* pass{};
    PipelineLayout* layout{};
    Shader* vertex{};
    Shader* fragment{};
    std::uint32_t vertexStride{};
    std::vector<VertexAttribute> attributes;
    std::uint32_t samples{1};
    std::uint32_t colorAttachmentCount{1}; // Zero for a depth-only pass.
    bool depthTest{}, depthWrite{};
    DepthCompare depthCompare{DepthCompare::Less};
    // Empty means Opaque for every color attachment; otherwise one entry per attachment.
    std::vector<BlendMode> colorBlends;
};
void validate(const GraphicsPipelineDesc& desc, bool independentBlend);
class GraphicsPipeline {
public:
    virtual ~GraphicsPipeline() = default;
};
struct ComputePipelineDesc {
    PipelineLayout* layout{};
    Shader* compute{};
};
void validate(const ComputePipelineDesc& desc);
class ComputePipeline {
public:
    virtual ~ComputePipeline() = default;
};
}
