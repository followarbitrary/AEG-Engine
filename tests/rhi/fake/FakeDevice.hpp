#pragma once
#include <RhiDevice.hpp>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace aeg::tests {
template <class Interface> struct EmptyResource final : Interface {};
struct FakeBuffer final : rhi::Buffer {
    rhi::BufferDesc desc;
    std::vector<std::byte> bytes;
    explicit FakeBuffer(rhi::BufferDesc value) : desc(value), bytes(value.size) {}
    const rhi::BufferDesc& description() const override { return desc; }
    std::span<std::byte> mapped() override { return bytes; }
};
struct FakeTexture final : rhi::Texture {
    rhi::TextureDesc desc;
    explicit FakeTexture(rhi::TextureDesc value) : desc(value) {}
    const rhi::TextureDesc& description() const override { return desc; }
};
struct FakePass final : rhi::RenderPass {
    rhi::RenderPassDesc desc;
    explicit FakePass(const rhi::RenderPassDesc& value) : desc(value) {}
};
struct FakeFramebuffer final : rhi::Framebuffer {
    rhi::FramebufferDesc desc;
    explicit FakeFramebuffer(const rhi::FramebufferDesc& value) : desc(value) {}
};
struct FakeSet final : rhi::BindingSet {
    std::vector<rhi::BindingWrite> writes;
    void update(std::span<const rhi::BindingWrite> value) override { writes.assign(value.begin(), value.end()); }
};
struct BoundSetSnapshot {
    rhi::PipelineLayout* layout{};
    std::uint32_t index{};
    rhi::BindingSet* set{};
    std::vector<rhi::BindingWrite> writes;
};
struct PassSnapshot {
    rhi::RenderPass* pass{};
    rhi::RenderPassDesc description;
    rhi::FramebufferDesc framebuffer;
    std::vector<rhi::TextureDesc> attachmentDescriptions;
    std::vector<BoundSetSnapshot> sets;
};
class FakeDevice final : public rhi::Device {
public:
    rhi::DeviceCapabilities caps{4096, 4096, 8, rhi::Format::D32Float};
    std::uint32_t samples{15};
    bool failTexture{};
    FakeBuffer* uniform{};
    std::vector<rhi::Format> uploadedFormats;
    rhi::Texture* lastReadback{};
    const rhi::DeviceCapabilities& capabilities() const override { return caps; }
    rhi::TextureSupport textureSupport(rhi::Format, rhi::TextureUsage) const override {
        return {true, samples, {4096, 4096}, true};
    }
    std::unique_ptr<rhi::Buffer> createBuffer(const rhi::BufferDesc& desc) override {
        rhi::validate(desc);
        auto result = std::make_unique<FakeBuffer>(desc);
        if (rhi::contains(desc.usage, rhi::BufferUsage::Uniform)) uniform = result.get();
        return result;
    }
    std::unique_ptr<rhi::Buffer> uploadBuffer(std::span<const std::byte> bytes, rhi::BufferUsage usage) override {
        auto result = createBuffer({bytes.size(), usage, rhi::MemoryUsage::Device});
        std::ranges::copy(bytes, result->mapped().begin());
        return result;
    }
    std::unique_ptr<rhi::Texture> createTexture(const rhi::TextureDesc& desc) override {
        rhi::validate(desc);
        if (std::exchange(failTexture, false)) throw std::runtime_error("Injected texture allocation failure");
        return std::make_unique<FakeTexture>(desc);
    }
    std::unique_ptr<rhi::Texture> uploadTexture(const rhi::TextureDesc& desc, std::span<const std::byte>) override {
        uploadedFormats.push_back(desc.format);
        return createTexture(desc);
    }
    std::unique_ptr<rhi::Sampler> createSampler(const rhi::SamplerDesc&) override {
        return std::make_unique<EmptyResource<rhi::Sampler>>();
    }
    std::unique_ptr<rhi::Shader> createShader(const rhi::ShaderDesc&) override {
        return std::make_unique<EmptyResource<rhi::Shader>>();
    }
    std::unique_ptr<rhi::BindingLayout> createBindingLayout(const rhi::BindingLayoutDesc&) override {
        return std::make_unique<EmptyResource<rhi::BindingLayout>>();
    }
    std::unique_ptr<rhi::BindingSet> createBindingSet(rhi::BindingLayout&,
                                                      std::span<const rhi::BindingWrite> writes) override {
        auto set = std::make_unique<FakeSet>();
        set->update(writes);
        return set;
    }
    std::unique_ptr<rhi::PipelineLayout> createPipelineLayout(const rhi::PipelineLayoutDesc&) override {
        return std::make_unique<EmptyResource<rhi::PipelineLayout>>();
    }
    std::unique_ptr<rhi::RenderPass> createRenderPass(const rhi::RenderPassDesc& desc) override {
        rhi::validate(desc);
        return std::make_unique<FakePass>(desc);
    }
    std::unique_ptr<rhi::Framebuffer> createFramebuffer(const rhi::FramebufferDesc& desc) override {
        return std::make_unique<FakeFramebuffer>(desc);
    }
    std::unique_ptr<rhi::GraphicsPipeline> createGraphicsPipeline(const rhi::GraphicsPipelineDesc&) override {
        return std::make_unique<EmptyResource<rhi::GraphicsPipeline>>();
    }
    rhi::TextureReadback readback(rhi::Texture& texture, rhi::ResourceState) override {
        lastReadback = &texture;
        const auto& desc = texture.description();
        return {desc.extent, desc.format,
                std::vector<std::byte>(static_cast<std::size_t>(desc.extent.width) * desc.extent.height *
                                       rhi::formatByteSize(desc.format))};
    }
    void waitIdle() override {}
    const std::string& deviceName() const override {
        static const std::string name{"Fake RHI"};
        return name;
    }
    std::uint32_t validationErrorCount() const override { return 0; }
};
class FakeCommands final : public rhi::CommandList {
public:
    std::vector<std::string> passes;
    std::vector<PassSnapshot> recordedPasses;
    std::vector<std::vector<std::byte>> constants;
    std::uint32_t indexedDraws{};
    void beginRenderPass(rhi::RenderPass& pass, rhi::Framebuffer& framebuffer, rhi::Extent2D,
                         std::span<const rhi::ClearValue>) override {
        if (activePass_) throw std::runtime_error("Nested fake render pass");
        const auto& desc = dynamic_cast<FakePass&>(pass).desc;
        passes.push_back(desc.depth ? "scene" : desc.colors.size() == 2 ? "taa" : "fxaa");
        recordedPasses.push_back({&pass, desc, dynamic_cast<FakeFramebuffer&>(framebuffer).desc, {}, {}});
        activePass_ = &recordedPasses.back();
        for (const auto texture : activePass_->framebuffer.attachments)
            activePass_->attachmentDescriptions.push_back(texture->description());
    }
    void endRenderPass() override { activePass_ = nullptr; }
    void setViewport(rhi::Extent2D) override {}
    void bindPipeline(rhi::GraphicsPipeline&) override {}
    void bindSet(rhi::PipelineLayout& layout, std::uint32_t index, rhi::BindingSet& set) override {
        if (!activePass_) throw std::runtime_error("Fake binding outside render pass");
        // Copy writes now: descriptor updates for a later frame must not rewrite this recording.
        activePass_->sets.push_back({&layout, index, &set, dynamic_cast<FakeSet&>(set).writes});
    }
    void bindVertexBuffer(rhi::Buffer&) override {}
    void bindIndexBuffer(rhi::Buffer&) override {}
    void pushConstants(rhi::PipelineLayout&, std::span<const std::byte> bytes) override {
        constants.emplace_back(bytes.begin(), bytes.end());
    }
    void draw(std::uint32_t) override {}
    void drawIndexed(std::uint32_t) override { ++indexedDraws; }
    void transition(rhi::Texture&, rhi::ResourceState, rhi::ResourceState) override {}
    void copyBuffer(rhi::Buffer&, rhi::Buffer&, std::size_t) override {}
    void copyTextureToBuffer(rhi::Texture&, rhi::Buffer&) override {}

private:
    PassSnapshot* activePass_{};
};
}
