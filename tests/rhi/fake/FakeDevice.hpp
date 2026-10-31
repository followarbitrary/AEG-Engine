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
struct FakeShader final : rhi::Shader {
    rhi::ShaderStage stage;
    explicit FakeShader(rhi::ShaderStage value) : stage(value) {}
};
struct FakeBindingLayout final : rhi::BindingLayout {
    rhi::BindingLayoutDesc desc;
    explicit FakeBindingLayout(const rhi::BindingLayoutDesc& value) : desc(value) {}
};
struct FakePipelineLayout final : rhi::PipelineLayout {
    rhi::PipelineLayoutDesc desc;
    explicit FakePipelineLayout(const rhi::PipelineLayoutDesc& value) : desc(value) {}
};
struct FakeComputePipeline final : rhi::ComputePipeline {
    rhi::ComputePipelineDesc desc;
    explicit FakeComputePipeline(const rhi::ComputePipelineDesc& value) : desc(value) {}
};
struct FakeFramebuffer final : rhi::Framebuffer {
    rhi::FramebufferDesc desc;
    explicit FakeFramebuffer(const rhi::FramebufferDesc& value) : desc(value) {}
};
struct FakeSet final : rhi::BindingSet {
    rhi::BindingLayoutDesc desc;
    std::vector<rhi::BindingWrite> writes;
    explicit FakeSet(rhi::BindingLayoutDesc value) : desc(std::move(value)) {}
    void update(std::span<const rhi::BindingWrite> value) override {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> seen;
        for (const auto& write : value) {
            const auto binding =
                std::ranges::find_if(desc.bindings, [&](const auto& entry) { return entry.binding == write.binding; });
            if (binding == desc.bindings.end() || !binding->arraySize || write.arrayElement >= binding->arraySize ||
                std::ranges::find(seen, std::pair{write.binding, write.arrayElement}) != seen.end())
                throw std::invalid_argument("Unknown or duplicate fake binding write");
            seen.emplace_back(write.binding, write.arrayElement);
            if (binding->type == rhi::BindingType::UniformBuffer) {
                if (!write.buffer || write.texture || write.sampler || write.storageMipLevel ||
                    !rhi::contains(write.buffer->description().usage, rhi::BufferUsage::Uniform))
                    throw std::invalid_argument("Invalid fake uniform binding");
            } else if (binding->type == rhi::BindingType::CombinedTextureSampler) {
                if (write.buffer || !write.texture || !write.sampler || write.storageMipLevel ||
                    !rhi::contains(write.texture->description().usage, rhi::TextureUsage::Sampled) ||
                    write.texture->description().samples != 1)
                    throw std::invalid_argument("Invalid fake sampled image binding");
            } else if (binding->type == rhi::BindingType::StorageImage) {
                if (write.buffer || !write.texture || write.sampler ||
                    !rhi::contains(write.texture->description().usage, rhi::TextureUsage::Storage) ||
                    write.texture->description().samples != 1 ||
                    write.storageMipLevel >= write.texture->description().mipLevels)
                    throw std::invalid_argument("Invalid fake storage image binding");
            } else if (binding->type == rhi::BindingType::StorageBuffer) {
                if (!write.buffer || write.texture || write.sampler || write.storageMipLevel ||
                    !rhi::contains(write.buffer->description().usage, rhi::BufferUsage::Storage))
                    throw std::invalid_argument("Invalid fake storage buffer binding");
            } else {
                throw std::invalid_argument("Unknown fake binding type");
            }
        }
        auto replacement = writes;
        for (const auto& write : value) {
            const auto found =
                std::ranges::find_if(replacement, [&](const auto& entry) {
                    return entry.binding == write.binding && entry.arrayElement == write.arrayElement;
                });
            if (found == replacement.end())
                replacement.push_back(write);
            else
                *found = write;
        }
        writes.swap(replacement);
    }
};
struct BoundSetSnapshot {
    rhi::PipelineLayout* layout{};
    std::uint32_t index{};
    rhi::BindingSet* set{};
    std::vector<rhi::BindingWrite> writes;
    rhi::PipelineBindPoint point{rhi::PipelineBindPoint::Graphics};
};
struct DispatchSnapshot {
    rhi::ComputePipelineDesc pipeline;
    std::vector<BoundSetSnapshot> sets;
    std::vector<std::byte> constants;
    std::array<std::uint32_t, 3> groups{};
};
struct TransitionSnapshot {
    rhi::Texture* texture{};
    rhi::ResourceState before{}, after{};
    rhi::TextureDesc description;
};
struct BufferTransitionSnapshot {
    rhi::Buffer* buffer{};
    rhi::ResourceState before{}, after{};
    rhi::BufferDesc description;
};
struct TextureCopySnapshot {
    rhi::Texture* source{};
    rhi::Buffer* destination{};
    rhi::TextureSubresource subresource;
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
class FakeCommands;
class FakeDevice final : public rhi::Device {
public:
    FakeDevice();
    ~FakeDevice() override;
    rhi::DeviceCapabilities caps{4096, 4096, 8, rhi::Format::D32Float, true};
    std::uint32_t samples{15};
    bool failTexture{};
    bool failFramebuffer{};
    // Zero disables the countdown; otherwise fail exactly that texture allocation.
    std::uint32_t failNthTexture{};
    bool failPipeline{};
    bool failBindingSet{};
    bool failWaitIdle{};
    bool failComputePipeline{}, failImmediateSubmit{};
    bool failSampler{}, failShader{}, failBindingLayout{}, failPipelineLayout{};
    std::uint32_t failNthComputePipeline{}, failNthBindingSet{};
    bool storageSupport{true}, cubeSupport{true}, storageLinearFiltering{true};
    std::uint32_t immediateSubmissions{};
    std::vector<std::function<void()>> deferredCallbacks;
    std::unique_ptr<FakeCommands> immediateCommands;
    void enableCompute() { caps.compute = true; }
    bool colorBlending{true};
    struct TextureSupportOverride {
        rhi::Format format;
        rhi::TextureUsage usage;
        rhi::TextureSupport support;
    };
    std::vector<TextureSupportOverride> textureSupportOverrides;
    // Independent sampled-depth capabilities: D32, D16, D24. Ordinary scene
    // depth support keeps the historical fake-device behavior.
    std::array<bool, 3> depthSampledSupport{true, true, true};
    std::array<rhi::Extent2D, 3> depthSampledMaximum{rhi::Extent2D{4096, 4096}, rhi::Extent2D{4096, 4096},
                                                     rhi::Extent2D{4096, 4096}};
    FakeBuffer* uniform{};
    std::vector<rhi::Format> uploadedFormats;
    rhi::Texture* lastReadback{};
    rhi::TextureSubresource lastReadbackSubresource{};
    const rhi::DeviceCapabilities& capabilities() const override { return caps; }
    rhi::TextureSupport
    textureSupport(rhi::Format format, rhi::TextureUsage usage,
                   rhi::TextureDimension dimension = rhi::TextureDimension::Texture2D) const override {
        for (const auto& entry : textureSupportOverrides)
            if (entry.format == format && entry.usage == usage) return entry.support;
        if (dimension != rhi::TextureDimension::Texture2D && dimension != rhi::TextureDimension::Cube)
            throw std::invalid_argument("Unknown fake texture dimension");
        if (dimension == rhi::TextureDimension::Cube && !cubeSupport) return {};
        if (rhi::contains(usage, rhi::TextureUsage::Storage))
            return {caps.compute && storageSupport, 1,
                    dimension == rhi::TextureDimension::Cube
                        ? rhi::Extent2D{caps.maximumCubeImageDimension, caps.maximumCubeImageDimension}
                        : rhi::Extent2D{caps.maximumImageDimension, caps.maximumImageDimension},
                    storageLinearFiltering, false};
        if (rhi::isDepthFormat(format) &&
            rhi::contains(usage, rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled)) {
            const auto index = format == rhi::Format::D32Float ? 0u : format == rhi::Format::D16Unorm ? 1u : 2u;
            return {depthSampledSupport[index], depthSampledSupport[index] ? 1u : 0u, depthSampledMaximum[index], false,
                    false};
        }
        const auto maximum = dimension == rhi::TextureDimension::Cube ? caps.maximumCubeImageDimension : 4096u;
        return {true, samples, {maximum, maximum}, true, colorBlending};
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
        if (failNthTexture && --failNthTexture == 0)
            throw std::runtime_error("Injected partial texture allocation failure");
        return std::make_unique<FakeTexture>(desc);
    }
    std::unique_ptr<rhi::Texture> uploadTexture(const rhi::TextureDesc& desc,
                                                std::span<const std::byte> pixels) override {
        rhi::validate(desc);
        if ((desc.format != rhi::Format::Rgba8Unorm && desc.format != rhi::Format::Rgba8Srgb &&
             desc.format != rhi::Format::Rgba32Float) ||
            desc.samples != 1 || !rhi::contains(desc.usage, rhi::TextureUsage::Sampled))
            throw std::invalid_argument("Fake upload requires sampled RGBA8 or RGBA32F faces");
        const auto pixelCount = static_cast<std::uint64_t>(desc.extent.width) * desc.extent.height;
        const auto pixelStride = rhi::formatByteSize(desc.format) * rhi::textureLayerCount(desc);
        if (pixelCount > std::numeric_limits<std::size_t>::max() / pixelStride ||
            pixelCount * pixelStride != pixels.size())
            throw std::invalid_argument("Fake texture upload byte count mismatch");
        uploadedFormats.push_back(desc.format);
        auto uploadedDesc = desc;
        uploadedDesc.usage = uploadedDesc.usage | rhi::TextureUsage::CopyDestination;
        auto texture = createTexture(uploadedDesc);
        dynamic_cast<FakeTexture&>(*texture).uploaded.assign(pixels.begin(), pixels.end());
        return texture;
    }
    std::unique_ptr<rhi::Sampler> createSampler(const rhi::SamplerDesc& desc) override {
        rhi::validate(desc);
        if (std::exchange(failSampler, false)) throw std::runtime_error("Injected sampler allocation failure");
        return std::make_unique<FakeSampler>(desc);
    }
    std::unique_ptr<rhi::Shader> createShader(const rhi::ShaderDesc& desc) override {
        if (desc.stage == rhi::ShaderStage::Compute && !caps.compute)
            throw std::runtime_error("Compute shaders unsupported");
        if (desc.stage != rhi::ShaderStage::Vertex && desc.stage != rhi::ShaderStage::Fragment &&
            desc.stage != rhi::ShaderStage::Compute)
            throw std::invalid_argument("Invalid fake shader stage");
        if (std::exchange(failShader, false)) throw std::runtime_error("Injected shader allocation failure");
        return std::make_unique<FakeShader>(desc.stage);
    }
    std::unique_ptr<rhi::BindingLayout> createBindingLayout(const rhi::BindingLayoutDesc& desc) override {
        if (std::exchange(failBindingLayout, false)) throw std::runtime_error("Injected binding layout failure");
        return std::make_unique<FakeBindingLayout>(desc);
    }
    std::unique_ptr<rhi::BindingSet> createBindingSet(rhi::BindingLayout& layout,
                                                      std::span<const rhi::BindingWrite> writes) override {
        if (std::exchange(failBindingSet, false)) throw std::runtime_error("Injected descriptor allocation failure");
        if (failNthBindingSet && --failNthBindingSet == 0)
            throw std::runtime_error("Injected partial descriptor allocation failure");
        auto set = std::make_unique<FakeSet>(dynamic_cast<FakeBindingLayout&>(layout).desc);
        set->update(writes);
        return set;
    }
    std::unique_ptr<rhi::PipelineLayout> createPipelineLayout(const rhi::PipelineLayoutDesc& desc) override {
        if (std::exchange(failPipelineLayout, false)) throw std::runtime_error("Injected pipeline layout failure");
        return std::make_unique<FakePipelineLayout>(desc);
    }
    std::unique_ptr<rhi::RenderPass> createRenderPass(const rhi::RenderPassDesc& desc) override {
        rhi::validate(desc);
        return std::make_unique<FakePass>(desc);
    }
    std::unique_ptr<rhi::Framebuffer> createFramebuffer(const rhi::FramebufferDesc& desc) override {
        if (std::exchange(failFramebuffer, false)) throw std::runtime_error("Injected framebuffer allocation failure");
        return std::make_unique<FakeFramebuffer>(desc);
    }
    std::unique_ptr<rhi::GraphicsPipeline> createGraphicsPipeline(const rhi::GraphicsPipelineDesc& desc) override {
        rhi::validate(desc, caps.independentBlend);
        if (std::exchange(failPipeline, false)) throw std::runtime_error("Injected pipeline creation failure");
        if (!colorBlending && std::ranges::find(desc.colorBlends, rhi::BlendMode::Alpha) != desc.colorBlends.end())
            throw std::runtime_error("Unsupported color attachment blending");
        return std::make_unique<FakePipeline>(desc);
    }
    std::unique_ptr<rhi::ComputePipeline> createComputePipeline(const rhi::ComputePipelineDesc& desc) override {
        rhi::validate(desc);
        if (!caps.compute) throw std::runtime_error("Compute pipelines unsupported");
        if (dynamic_cast<FakeShader&>(*desc.compute).stage != rhi::ShaderStage::Compute)
            throw std::invalid_argument("Compute pipeline requires a compute shader");
        if (std::exchange(failComputePipeline, false)) throw std::runtime_error("Injected compute pipeline failure");
        if (failNthComputePipeline && --failNthComputePipeline == 0)
            throw std::runtime_error("Injected partial compute pipeline failure");
        return std::make_unique<FakeComputePipeline>(desc);
    }
    void submitImmediate(const std::function<void(rhi::CommandList&)>& record) override;
    void deferUntilIdle(std::function<void()> callback) override {
        if (!callback) throw std::invalid_argument("Missing fake deferred callback");
        deferredCallbacks.push_back(std::move(callback));
    }
    rhi::TextureReadback readback(rhi::Texture& texture, rhi::ResourceState = rhi::ResourceState::Sampled,
                                  rhi::TextureSubresource subresource = {}) override {
        lastReadback = &texture;
        lastReadbackSubresource = subresource;
        const auto& desc = texture.description();
        const auto extent = rhi::mipExtent(desc, subresource.mipLevel);
        if (subresource.layer >= rhi::textureLayerCount(desc))
            throw std::invalid_argument("Readback layer out of range");
        return {extent, desc.format,
                std::vector<std::byte>(static_cast<std::size_t>(extent.width) * extent.height *
                                       rhi::formatByteSize(desc.format))};
    }
    void waitIdle() override {
        if (std::exchange(failWaitIdle, false)) throw std::runtime_error("Injected device wait failure");
        auto callbacks = std::exchange(deferredCallbacks, {});
        for (auto& callback : callbacks) callback();
    }
    const std::string& deviceName() const override {
        static const std::string name{"Fake RHI"};
        return name;
    }
    std::uint32_t validationErrorCount() const override { return 0; }
};
class FakeCommands final : public rhi::CommandList {
public:
    explicit FakeCommands(const rhi::DeviceCapabilities* caps = nullptr) : caps_(caps) {}
    std::vector<std::string> passes;
    std::vector<PassSnapshot> recordedPasses;
    std::vector<std::vector<std::byte>> constants;
    std::uint32_t indexedDraws{};
    std::vector<DispatchSnapshot> dispatches;
    std::vector<TransitionSnapshot> transitions;
    std::vector<BufferTransitionSnapshot> bufferTransitions;
    std::vector<TextureCopySnapshot> subresourceCopies;
    std::vector<std::string> operations;
    std::vector<std::string> debugLabels;
    std::vector<std::string> gpuPasses;
    rhi::Framebuffer* lastFramebuffer{};
    bool inRenderPass() const { return activePass_ != nullptr; }
    void beginRenderPass(rhi::RenderPass& pass, rhi::Framebuffer& framebuffer, rhi::Extent2D,
                         std::span<const rhi::ClearValue> clears) override {
        if (activePass_) throw std::runtime_error("Nested fake render pass");
        const auto& desc = dynamic_cast<FakePass&>(pass).desc;
        passes.push_back(desc.depth                ? (desc.colors.empty() ? "shadow" : "scene")
                         : desc.colors.size() == 2 ? "taa"
                                                   : "fxaa");
        recordedPasses.push_back({&pass, desc, dynamic_cast<FakeFramebuffer&>(framebuffer).desc, {}, {}});
        activePass_ = &recordedPasses.back();
        lastFramebuffer = &framebuffer;
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
        if (!activePass_->description.depth && activePass_->description.colors.size() == 1) {
            const auto& layout = dynamic_cast<FakePipelineLayout&>(*activePass_->pipeline.layout).desc;
            passes.back() = layout.pushConstantBytes == 16 ? "tone" : "fxaa";
        }
    }
    void bindComputePipeline(rhi::ComputePipeline& pipeline) override {
        if (activePass_ || (caps_ && !caps_->compute)) throw std::runtime_error("Invalid fake compute pipeline bind");
        computePipeline_ = &dynamic_cast<FakeComputePipeline&>(pipeline);
        if (computeLayout_ != computePipeline_->desc.layout) computeSets_.clear();
        computeLayout_ = computePipeline_->desc.layout;
    }
    void bindSet(rhi::PipelineLayout& layout, std::uint32_t index, rhi::BindingSet& set,
                 rhi::PipelineBindPoint point = rhi::PipelineBindPoint::Graphics) override {
        if (point == rhi::PipelineBindPoint::Compute) {
            if (activePass_ || (caps_ && !caps_->compute))
                throw std::runtime_error("Fake compute binding inside render pass");
            const auto& desc = dynamic_cast<FakePipelineLayout&>(layout).desc;
            if (index >= desc.sets.size()) throw std::invalid_argument("Fake compute set index exceeds layout");
            const auto& expected = dynamic_cast<FakeBindingLayout&>(*desc.sets[index]).desc;
            const auto& actual = dynamic_cast<FakeSet&>(set).desc;
            if (expected.bindings.size() != actual.bindings.size())
                throw std::invalid_argument("Fake compute binding layout mismatch");
            for (const auto& binding : expected.bindings)
                if (!std::ranges::any_of(actual.bindings, [&](const auto& other) {
                        return binding.binding == other.binding && binding.type == other.type &&
                               binding.stages == other.stages && binding.arraySize == other.arraySize;
                    }))
                    throw std::invalid_argument("Fake compute binding layout mismatch");
            if (computePipeline_ && computePipeline_->desc.layout != &layout)
                throw std::invalid_argument("Fake compute pipeline layout mismatch");
            if (computeLayout_ != &layout) computeSets_.clear();
            computeLayout_ = &layout;
            if (computeSets_.size() <= index) computeSets_.resize(index + 1);
            computeSets_[index] = {&layout, index, &set, {}, point};
            return;
        }
        if (point != rhi::PipelineBindPoint::Graphics) throw std::invalid_argument("Unknown fake pipeline bind point");
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
    void draw(std::uint32_t) override {
        if (!activePass_) throw std::runtime_error("Fake draw outside render pass");
        activePass_->draws.push_back({activePass_->pipeline, currentConstants_, nullptr});
    }
    void drawIndexed(std::uint32_t) override {
        if (!activePass_) throw std::runtime_error("Fake draw outside render pass");
        ++indexedDraws;
        activePass_->draws.push_back({activePass_->pipeline, currentConstants_, indices_});
    }
    void drawIndexedIndirect(rhi::Buffer& buffer, std::size_t offset, std::uint32_t drawCount,
                             std::uint32_t stride) override {
        if (!activePass_ || (caps_ && !caps_->indirectDraw) || !indices_ || !drawCount || offset % 4 ||
            stride < sizeof(std::uint32_t) * 5 ||
            stride % 4 || !rhi::contains(buffer.description().usage, rhi::BufferUsage::Indirect) ||
            offset > buffer.description().size ||
            drawCount > (buffer.description().size - offset) / stride)
            throw std::invalid_argument("Invalid fake indirect draw");
        indexedDraws += drawCount;
        for (std::uint32_t i = 0; i < drawCount; ++i)
            activePass_->draws.push_back({activePass_->pipeline, currentConstants_, indices_});
    }
    void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override {
        if (activePass_ || !computePipeline_ || !x || !y || !z)
            throw std::invalid_argument("Invalid fake dispatch state or group count");
        const std::array groups{x, y, z};
        if (caps_)
            for (std::size_t i = 0; i < groups.size(); ++i)
                if (groups[i] > caps_->maximumComputeGroupCount[i])
                    throw std::invalid_argument("Fake dispatch exceeds limits");
        const auto& layout = dynamic_cast<FakePipelineLayout&>(*computePipeline_->desc.layout).desc;
        if (computeSets_.size() < layout.sets.size())
            throw std::invalid_argument("Missing fake compute descriptor set");
        auto sets = computeSets_;
        for (std::size_t i = 0; i < layout.sets.size(); ++i) {
            if (!sets[i].set) throw std::invalid_argument("Missing fake compute descriptor set");
            const auto& set = dynamic_cast<FakeSet&>(*sets[i].set);
            for (const auto& binding : set.desc.bindings)
                if (rhi::contains(binding.stages, rhi::ShaderStage::Compute))
                    for (std::uint32_t element = 0; element < binding.arraySize; ++element)
                        if (!std::ranges::any_of(set.writes, [&](const auto& write) {
                                return write.binding == binding.binding && write.arrayElement == element;
                            }))
                            throw std::invalid_argument("Uninitialized fake compute binding");
            sets[i].writes = set.writes;
        }
        dispatches.push_back({computePipeline_->desc, std::move(sets), currentConstants_, groups});
        operations.push_back("dispatch");
    }
    void transition(rhi::Texture& texture, rhi::ResourceState before, rhi::ResourceState after) override {
        if (activePass_ || after == rhi::ResourceState::Undefined)
            throw std::invalid_argument("Invalid fake transition");
        transitions.push_back({&texture, before, after, texture.description()});
        operations.push_back("transition");
    }
    void transition(rhi::Buffer& buffer, rhi::ResourceState before, rhi::ResourceState after) override {
        if (activePass_ || after == rhi::ResourceState::Undefined ||
            (before != rhi::ResourceState::Storage && before != rhi::ResourceState::Indirect &&
             before != rhi::ResourceState::CopySource && before != rhi::ResourceState::Undefined) ||
            (after != rhi::ResourceState::Storage && after != rhi::ResourceState::Indirect &&
             after != rhi::ResourceState::CopySource))
            throw std::invalid_argument("Invalid fake buffer transition");
        if ((before == rhi::ResourceState::Storage || after == rhi::ResourceState::Storage) &&
            !rhi::contains(buffer.description().usage, rhi::BufferUsage::Storage))
            throw std::invalid_argument("Storage transition requires storage buffer usage");
        if (after == rhi::ResourceState::Indirect &&
            !rhi::contains(buffer.description().usage, rhi::BufferUsage::Indirect))
            throw std::invalid_argument("Indirect transition requires indirect buffer usage");
        bufferTransitions.push_back({&buffer, before, after, buffer.description()});
        operations.push_back("buffer-transition");
    }
    void copyBuffer(rhi::Buffer&, rhi::Buffer&, std::size_t) override {}
    void copyTextureToBuffer(rhi::Texture& texture, rhi::Buffer& buffer,
                             rhi::TextureSubresource subresource = {}) override {
        (void)rhi::mipExtent(texture.description(), subresource.mipLevel);
        if (activePass_ || subresource.layer >= rhi::textureLayerCount(texture.description()))
            throw std::invalid_argument("Invalid fake subresource copy");
        subresourceCopies.push_back({&texture, &buffer, subresource});
    }
    void beginDebugLabel(std::string_view name) override { debugLabels.emplace_back(name); }
    void endDebugLabel() override {}
    void beginGpuPass(std::string_view name) override { gpuPasses.emplace_back(name); }
    void endGpuPass() override {}

private:
    PassSnapshot* activePass_{};
    rhi::Buffer* indices_{};
    std::vector<std::byte> currentConstants_;
    const rhi::DeviceCapabilities* caps_{};
    FakeComputePipeline* computePipeline_{};
    rhi::PipelineLayout* computeLayout_{};
    std::vector<BoundSetSnapshot> computeSets_;
};
inline FakeDevice::FakeDevice() {
    caps.maximumCubeImageDimension = 4096;
    caps.maximumComputeGroupCount = {65535, 65535, 65535};
    caps.maximumComputeGroupSize = {1024, 1024, 64};
    caps.maximumComputeInvocations = 1024;
}
inline FakeDevice::~FakeDevice() = default;
inline void FakeDevice::submitImmediate(const std::function<void(rhi::CommandList&)>& record) {
    if (!record) throw std::invalid_argument("Missing fake immediate callback");
    auto commands = std::make_unique<FakeCommands>(&caps);
    record(*commands);
    if (commands->inRenderPass()) throw std::logic_error("Fake immediate render pass was not ended");
    if (std::exchange(failImmediateSubmit, false)) throw std::runtime_error("Injected immediate submission failure");
    if (!immediateCommands) {
        immediateCommands = std::move(commands);
    } else {
        immediateCommands->dispatches.insert(immediateCommands->dispatches.end(), commands->dispatches.begin(),
                                             commands->dispatches.end());
        immediateCommands->transitions.insert(immediateCommands->transitions.end(), commands->transitions.begin(),
                                              commands->transitions.end());
        immediateCommands->bufferTransitions.insert(immediateCommands->bufferTransitions.end(),
                                                    commands->bufferTransitions.begin(),
                                                    commands->bufferTransitions.end());
        immediateCommands->subresourceCopies.insert(immediateCommands->subresourceCopies.end(),
                                                    commands->subresourceCopies.begin(),
                                                    commands->subresourceCopies.end());
        immediateCommands->operations.insert(immediateCommands->operations.end(), commands->operations.begin(),
                                             commands->operations.end());
    }
    ++immediateSubmissions;
}
}
