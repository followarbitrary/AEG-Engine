#pragma once

#include <RhiSwapchain.hpp>
#include <RhiCommandList.hpp>
#include <RhiDevice.hpp>
#include <RhiTexture.hpp>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace aeg::render {

using FrameToken = std::uint64_t;
class TransientTexturePool;

class RenderGraph {
public:
    using TextureDesc = rhi::TextureDesc;

    struct Handle {
        std::uint32_t index{~std::uint32_t{0}};
        std::uint32_t generation{};
        friend bool operator==(const Handle&, const Handle&) = default;
        friend bool operator!=(const Handle&, const Handle&) = default;
        explicit operator bool() const { return generation != 0 && index != ~std::uint32_t{0}; }
    };

    enum class Usage {
        SampledRead,
        ColorWrite,
        DepthWrite,
        StorageRead,
        StorageWrite,
        StorageReadWrite,
        CopySrc,
        CopyDst,
        // Present is a callback-free graphics state transition. It must be
        // the only declaration in its pass; the imported swapchain's final
        // state is also set to Present automatically.
        Present,
    };

    enum class PassType { Graphics, Compute, Copy };
    enum class PassFlags : std::uint32_t { None = 0, NeverCull = 1u << 0 };
    friend constexpr PassFlags operator|(PassFlags a, PassFlags b) {
        return static_cast<PassFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }
    friend constexpr bool contains(PassFlags flags, PassFlags value) {
        return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(value)) ==
               static_cast<std::uint32_t>(value);
    }

    struct PassContext {
        [[nodiscard]] rhi::Texture& texture(Handle handle) const {
            if (!resolver_) throw std::logic_error("RenderGraph PassContext is not active");
            auto* resolved = resolver_(handle);
            if (!resolved) throw std::invalid_argument("RenderGraph handle has no physical texture");
            return *resolved;
        }

    private:
        friend class RenderGraph;
        std::function<rhi::Texture*(Handle)> resolver_;
    };
    using ExecuteCallback = std::function<void(PassContext&)>;

    struct Barrier {
        Handle resource;
        rhi::ResourceState before{rhi::ResourceState::Undefined};
        rhi::ResourceState after{rhi::ResourceState::Undefined};
        std::size_t passIndex{};
    };
    struct UsageRecord {
        Handle resource;
        Usage usage{};
        bool read{};
        bool write{};
    };
    struct CompiledPass {
        std::size_t originalIndex{};
        std::string name;
        PassType type{PassType::Graphics};
        PassFlags flags{PassFlags::None};
        bool culled{};
        std::vector<UsageRecord> usages;
        std::vector<Barrier> preBarriers;
        std::vector<Barrier> postBarriers;
    };
    struct CompiledResource {
        Handle handle;
        std::string name;
        TextureDesc description;
        bool imported{};
        bool transient{};
        bool swapchain{};
        std::size_t firstUse{};
        std::size_t lastUse{};
    };
    struct CompileResult {
        std::vector<CompiledPass> passes;
        std::vector<std::size_t> retainedPassOrder;
        std::vector<CompiledPass> allPasses;
        std::vector<CompiledResource> resources;
    };

    class PassBuilder {
    public:
        PassBuilder() = default;
        PassBuilder& read(Handle resource, Usage usage);
        PassBuilder& write(Handle resource, Usage usage);
        PassBuilder& readWrite(Handle resource, Usage usage = Usage::StorageReadWrite);
        PassBuilder& clear(rhi::ClearValue value);
        PassBuilder& setFinalState(Handle resource, rhi::ResourceState state);
        PassBuilder& setExternalRenderPass(rhi::RenderPass& pass, rhi::Framebuffer& framebuffer,
                                            rhi::Extent2D extent);
        PassBuilder& setExternalRenderPass(rhi::RenderPass& pass, rhi::Extent2D extent,
                                            std::unique_ptr<rhi::Framebuffer>* framebufferStorage = nullptr);

    private:
        friend class RenderGraph;
        PassBuilder(RenderGraph* graph, std::size_t index) : graph_(graph), index_(index) {}
        RenderGraph* graph_{};
        std::size_t index_{};
    };

    RenderGraph();
    ~RenderGraph();
    RenderGraph(RenderGraph&&) noexcept;
    RenderGraph& operator=(RenderGraph&&) noexcept;
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    Handle createTexture(const rhi::TextureDesc& description, std::string name, bool transient = false);
    Handle importTexture(rhi::Texture& texture, rhi::ResourceState& trackedState, std::string name);
    Handle importSwapchain(rhi::Swapchain& swapchain, std::string name);

    PassBuilder addPass(std::string name, PassType type, PassFlags flags, ExecuteCallback execute = {});
    template <class F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, ExecuteCallback>)
    PassBuilder addPass(std::string name, PassType type, PassFlags flags, F&& execute) {
        ExecuteCallback callback;
        if constexpr (std::is_invocable_v<F&, PassContext&>) {
            callback = std::forward<F>(execute);
        } else if constexpr (std::is_invocable_v<F&>) {
            callback = [fn = std::forward<F>(execute)](PassContext&) mutable { fn(); };
        } else {
            static_assert(std::is_invocable_v<F&, PassContext&>, "RenderGraph callback must be callable");
        }
        return addPass(std::move(name), type, flags, std::move(callback));
    }

    CompileResult compile(std::span<const Handle> outputs);
    CompileResult compile(std::initializer_list<Handle> outputs) { return compile(std::span(outputs)); }
    void execute(rhi::CommandList&, rhi::Device&, TransientTexturePool&, FrameToken);
    void reset();
    [[nodiscard]] std::string debugDump() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aeg::render
