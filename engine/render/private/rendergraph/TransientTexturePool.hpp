#pragma once

#include <RenderGraph.hpp>
#include <RhiDevice.hpp>
#include <memory>
#include <vector>

namespace aeg::render {

class TransientTexturePool {
public:
    explicit TransientTexturePool(rhi::Device& device) : device_(device) {}
    ~TransientTexturePool() noexcept;

    TransientTexturePool(const TransientTexturePool&) = delete;
    TransientTexturePool& operator=(const TransientTexturePool&) = delete;

    rhi::Texture& acquire(const RenderGraph::TextureDesc& description);
    [[nodiscard]] std::shared_ptr<rhi::Texture> lease(rhi::Texture& texture) const;
    void retire(rhi::Texture&, FrameToken);
    void retireRenderObjects(std::unique_ptr<rhi::RenderPass> pass,
                             std::unique_ptr<rhi::Framebuffer> framebuffer, FrameToken);
    void releaseImmediately(rhi::Texture&);
    void reclaimCompletedFrame(FrameToken);
    [[nodiscard]] std::size_t poolHitCount() const { return poolHits_; }
    [[nodiscard]] std::size_t retiredRenderObjectCount() const { return retiredRenderObjects_.size(); }

private:
    struct Entry {
        std::shared_ptr<rhi::Texture> texture;
        RenderGraph::TextureDesc description;
        std::optional<FrameToken> retired;
        bool immediatelyAvailable{};
    };
    struct RetiredRenderObjects {
        std::unique_ptr<rhi::RenderPass> pass;
        std::unique_ptr<rhi::Framebuffer> framebuffer;
        FrameToken token{};
    };

    static bool compatible(const RenderGraph::TextureDesc&, const RenderGraph::TextureDesc&);
    rhi::Device& device_;
    std::vector<Entry> entries_;
    std::vector<RetiredRenderObjects> retiredRenderObjects_;
    FrameToken completedToken_{};
    std::size_t poolHits_{};
};

} // namespace aeg::render
