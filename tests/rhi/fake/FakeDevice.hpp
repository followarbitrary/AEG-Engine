#pragma once
#include <RhiDevice.hpp>
#include <algorithm>
#include <array>
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
    std::vector<std::byte> uploaded;
    explicit FakeTexture(rhi::TextureDesc value) : desc(value) {}
    const rhi::TextureDesc& description() const override { return desc; }
};
struct FakeSampler final : rhi::Sampler {
    rhi::SamplerDesc desc;
    explicit FakeSampler(rhi::SamplerDesc value) : desc(value) {}
};
struct FakePass final : rhi::RenderPass {
    rhi::RenderPassDesc desc;
    explicit FakePass(const rhi::RenderPassDesc& value) : desc(value) {}
};
struct FakePipeline final : rhi::GraphicsPipeline {
    rhi::GraphicsPipelineDesc desc;
    explicit FakePipeline(const rhi::GraphicsPipelineDesc& value) : desc(value) {}
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
struct DrawSnapshot {
    rhi::GraphicsPipelineDesc pipeline;
    std::vector<std::byte> constants;
    rhi::Buffer* indices{};
};
struct PassSnapshot {
    rhi::RenderPass* pass{};
    rhi::RenderPassDesc description;
    rhi::FramebufferDesc framebuffer;
    std::vector<rhi::TextureDesc> attachmentDescriptions;
    std::vector<BoundSetSnapshot> sets;
    std::vector<rhi::ClearValue> clears;
    rhi::GraphicsPipelineDesc pipeline;
    std::vector<DrawSnapshot> draws;
};
class FakeDevice final : public rhi::Device {
public:
    rhi::DeviceCapabilities caps{4096, 4096, 8, rhi::Format::D32Float, true};
    std::uint32_t samples{15};
    bool failTexture{};
    bool failPipeline{};
    bool failBindingSet{};
    bool failWaitIdle{};
    bool colorBlending{true};
    // Independent sampled-depth capabilities: D32, D16, D24. Ordinary scene
    // depth support keeps the historical fake-device behavior.
    std::array<bool, 3> depthSampledSupport{true, true, true};
    std::array<rhi::Extent2D, 3> depthSampledMaximum{rhi::Extent2D{4096, 4096}, rhi::Extent2D{4096, 4096},
                                                     rhi::Extent2D{4096, 4096}};
    FakeBuffer* uniform{};
    std::vector<rhi::Format> uploadedFormats;
    rhi::Texture* lastReadback{};
    const rhi::DeviceCapabilities& capabilities() const override { return caps; }
    rhi::TextureSupport textureSupport(rhi::Format format, rhi::TextureUsage usage) const override {
        if (rhi::isDepthFormat(format) &&
            rhi::contains(usage, rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled)) {
            const auto index = format == rhi::Format::D32Float ? 0u : format == rhi::Format::D16Unorm ? 1u : 2u;
            return {depthSampledSupport[index], depthSampledSupport[index] ? 1u : 0u, depthSampledMaximum[index], false,
                    false};
        }
        return {true, samples, {4096, 4096}, true, colorBlending};
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
    std::unique_ptr<rhi::Texture> uploadTexture(const rhi::TextureDesc& desc,
                                                std::span<const std::byte> pixels) override {
        uploadedFormats.push_back(desc.format);
        auto texture = createTexture(desc);
        dynamic_cast<FakeTexture&>(*texture).uploaded.assign(pixels.begin(), pixels.end());
        return texture;
    }
    std::unique_ptr<rhi::Sampler> createSampler(const rhi::SamplerDesc& desc) override {
        return std::make_unique<FakeSampler>(desc);
    }
    std::unique_ptr<rhi::Shader> createShader(const rhi::ShaderDesc&) override {
        return std::make_unique<EmptyResource<rhi::Shader>>();
    }
    std::unique_ptr<rhi::BindingLayout> createBindingLayout(const rhi::BindingLayoutDesc&) override {
        return std::make_unique<EmptyResource<rhi::BindingLayout>>();
    }
    std::unique_ptr<rhi::BindingSet> createBindingSet(rhi::BindingLayout&,
                                                      std::span<const rhi::BindingWrite> writes) override {
        if (std::exchange(failBindingSet, false)) throw std::runtime_error("Injected descriptor allocation failure");
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
    std::unique_ptr<rhi::GraphicsPipeline> createGraphicsPipeline(const rhi::GraphicsPipelineDesc& desc) override {
        rhi::validate(desc, caps.independentBlend);
        if (std::exchange(failPipeline, false)) throw std::runtime_error("Injected pipeline creation failure");
        if (!colorBlending && std::ranges::find(desc.colorBlends, rhi::BlendMode::Alpha) != desc.colorBlends.end())
            throw std::runtime_error("Unsupported color attachment blending");
        return std::make_unique<FakePipeline>(desc);
    }
    rhi::TextureReadback readback(rhi::Texture& texture, rhi::ResourceState) override {
        lastReadback = &texture;
        const auto& desc = texture.description();
        return {desc.extent, desc.format,
                std::vector<std::byte>(static_cast<std::size_t>(desc.extent.width) * desc.extent.height *
                                       rhi::formatByteSize(desc.format))};
    }
    void waitIdle() override {
        if (std::exchange(failWaitIdle, false)) throw std::runtime_error("Injected device wait failure");
    }
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
                         std::span<const rhi::ClearValue> clears) override {
        if (activePass_) throw std::runtime_error("Nested fake render pass");
        const auto& desc = dynamic_cast<FakePass&>(pass).desc;
        passes.push_back(desc.depth                ? (desc.colors.empty() ? "shadow" : "scene")
                         : desc.colors.size() == 2 ? "taa"
                                                   : "fxaa");
        recordedPasses.push_back({&pass, desc, dynamic_cast<FakeFramebuffer&>(framebuffer).desc, {}, {}});
        activePass_ = &recordedPasses.back();
        activePass_->clears.assign(clears.begin(), clears.end());
        for (const auto texture : activePass_->framebuffer.attachments)
            activePass_->attachmentDescriptions.push_back(texture->description());
    }
    void endRenderPass() override { activePass_ = nullptr; }
    void setViewport(rhi::Extent2D) override {}
    void bindPipeline(rhi::GraphicsPipeline& pipeline) override {
        if (!activePass_) throw std::runtime_error("Fake pipeline outside render pass");
        activePass_->pipeline = dynamic_cast<FakePipeline&>(pipeline).desc;
        if (activePass_->pipeline.pass != activePass_->pass)
            throw std::runtime_error("Pipeline belongs to the wrong render pass");
    }
    void bindSet(rhi::PipelineLayout& layout, std::uint32_t index, rhi::BindingSet& set) override {
        if (!activePass_) throw std::runtime_error("Fake binding outside render pass");
        // Copy writes now: descriptor updates for a later frame must not rewrite this recording.
        activePass_->sets.push_back({&layout, index, &set, dynamic_cast<FakeSet&>(set).writes});
    }
    void bindVertexBuffer(rhi::Buffer&) override {}
    void bindIndexBuffer(rhi::Buffer& buffer) override { indices_ = &buffer; }
    void pushConstants(rhi::PipelineLayout&, std::span<const std::byte> bytes) override {
        constants.emplace_back(bytes.begin(), bytes.end());
        currentConstants_.assign(bytes.begin(), bytes.end());
    }
    void draw(std::uint32_t) override {}
    void drawIndexed(std::uint32_t) override {
        if (!activePass_) throw std::runtime_error("Fake draw outside render pass");
        ++indexedDraws;
        activePass_->draws.push_back({activePass_->pipeline, currentConstants_, indices_});
    }
    void transition(rhi::Texture&, rhi::ResourceState, rhi::ResourceState) override {}
    void copyBuffer(rhi::Buffer&, rhi::Buffer&, std::size_t) override {}
    void copyTextureToBuffer(rhi::Texture&, rhi::Buffer&) override {}

private:
    PassSnapshot* activePass_{};
    rhi::Buffer* indices_{};
    std::vector<std::byte> currentConstants_;
};
}
