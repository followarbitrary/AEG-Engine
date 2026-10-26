#pragma once
#include <RhiCommandList.hpp>
#include <memory>
#include <string>
namespace aeg::rhi {
class Device {
public:
    Device() = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    virtual ~Device() = default;
    [[nodiscard]] virtual const DeviceCapabilities& capabilities() const = 0;
    [[nodiscard]] virtual TextureSupport textureSupport(Format format, TextureUsage usage) const = 0;
    virtual std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc) = 0;
    virtual std::unique_ptr<Buffer> uploadBuffer(std::span<const std::byte> bytes, BufferUsage usage) = 0;
    virtual std::unique_ptr<Texture> createTexture(const TextureDesc& desc) = 0;
    virtual std::unique_ptr<Texture> uploadTexture(const TextureDesc& desc, std::span<const std::byte> pixels) = 0;
    virtual std::unique_ptr<Sampler> createSampler(const SamplerDesc& desc) = 0;
    virtual std::unique_ptr<Shader> createShader(const ShaderDesc& desc) = 0;
    virtual std::unique_ptr<BindingLayout> createBindingLayout(const BindingLayoutDesc& desc) = 0;
    virtual std::unique_ptr<BindingSet> createBindingSet(BindingLayout& layout,
                                                         std::span<const BindingWrite> writes) = 0;
    virtual std::unique_ptr<PipelineLayout> createPipelineLayout(const PipelineLayoutDesc& desc) = 0;
    virtual std::unique_ptr<RenderPass> createRenderPass(const RenderPassDesc& desc) = 0;
    virtual std::unique_ptr<Framebuffer> createFramebuffer(const FramebufferDesc& desc) = 0;
    virtual std::unique_ptr<GraphicsPipeline> createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual TextureReadback readback(Texture& texture, ResourceState currentState = ResourceState::Sampled) = 0;
    virtual void waitIdle() = 0;
    [[nodiscard]] virtual const std::string& deviceName() const = 0;
    [[nodiscard]] virtual std::uint32_t validationErrorCount() const = 0;
};
}
