#include "TransientTexturePool.hpp"

#include <algorithm>
#include <stdexcept>

namespace aeg::render {

TransientTexturePool::~TransientTexturePool() noexcept {
    if (retiredRenderObjects_.empty()) return;
    auto pending = std::make_shared<std::vector<RetiredRenderObjects>>(std::move(retiredRenderObjects_));
    try {
        device_.deferUntilIdle([pending = std::move(pending)]() mutable { pending->clear(); });
    } catch (...) {
        // A backend that cannot defer destruction is expected to have no
        // in-flight work when the pool itself is destroyed.
        pending->clear();
    }
}

bool TransientTexturePool::compatible(const RenderGraph::TextureDesc& lhs,
                                      const RenderGraph::TextureDesc& rhs) {
    return lhs.extent == rhs.extent && lhs.format == rhs.format && lhs.usage == rhs.usage &&
           lhs.samples == rhs.samples && lhs.mipLevels == rhs.mipLevels && lhs.dimension == rhs.dimension;
}

rhi::Texture& TransientTexturePool::acquire(const RenderGraph::TextureDesc& description) {
    rhi::validate(description);
    for (auto& entry : entries_) {
        if (!compatible(entry.description, description)) continue;
        if (!entry.immediatelyAvailable && entry.retired) {
            const auto& retirement = *entry.retired;
            const bool complete = retirement.slotAware
                                      ? retirement.slot < completedSlots_.size() &&
                                            retirement.token < completedSlots_[retirement.slot]
                                      : retirement.token < completedToken_;
            if (!complete) continue;
        } else if (!entry.immediatelyAvailable && !entry.retired) {
            continue;
        }
        entry.retired.reset();
        entry.immediatelyAvailable = false;
        ++poolHits_;
        return *entry.texture;
    }
    auto texture = device_.createTexture(description);
    if (!texture) throw std::runtime_error("Transient texture allocation returned null");
    entries_.push_back({std::shared_ptr<rhi::Texture>(std::move(texture)), description, std::nullopt, false});
    return *entries_.back().texture;
}

std::shared_ptr<rhi::Texture> TransientTexturePool::lease(rhi::Texture& texture) const {
    const auto found = std::ranges::find_if(entries_, [&](const auto& entry) { return entry.texture.get() == &texture; });
    if (found == entries_.end()) throw std::invalid_argument("Transient pool cannot lease an unknown texture");
    return found->texture;
}

void TransientTexturePool::retire(rhi::Texture& texture, FrameToken token) {
    const auto found = std::ranges::find_if(entries_, [&](const auto& entry) { return entry.texture.get() == &texture; });
    if (found == entries_.end()) throw std::invalid_argument("Transient pool cannot retire an unknown texture");
    found->retired = Entry::Retirement{token, activeSlot_, slotAware_};
    found->immediatelyAvailable = false;
}

void TransientTexturePool::retireRenderObjects(std::unique_ptr<rhi::RenderPass> pass,
                                               std::unique_ptr<rhi::Framebuffer> framebuffer, FrameToken token) {
    if (!pass && !framebuffer) return;
    retiredRenderObjects_.push_back({std::move(pass), std::move(framebuffer), token, activeSlot_, slotAware_});
}

void TransientTexturePool::releaseImmediately(rhi::Texture& texture) {
    const auto found = std::ranges::find_if(entries_, [&](const auto& entry) { return entry.texture.get() == &texture; });
    if (found == entries_.end()) throw std::invalid_argument("Transient pool cannot release an unknown texture");
    found->retired = Entry::Retirement{completedToken_, 0, false};
    found->immediatelyAvailable = true;
}

void TransientTexturePool::beginFrame(FrameToken token, std::uint32_t slot, std::uint32_t slotCount) {
    if (!slotCount || slot >= slotCount) throw std::invalid_argument("Transient pool frame slot is out of range");
    slotAware_ = true;
    activeSlot_ = slot;
    if (completedSlots_.size() != slotCount) completedSlots_.resize(slotCount);
    completedSlots_[slot] = std::max(completedSlots_[slot], token);
    std::erase_if(retiredRenderObjects_, [&](const auto& objects) {
        if (objects.slotAware)
            return objects.slot < completedSlots_.size() && objects.token < completedSlots_[objects.slot];
        return objects.token < completedToken_;
    });
}

void TransientTexturePool::reclaimCompletedFrame(FrameToken token) {
    completedToken_ = std::max(completedToken_, token);
    std::erase_if(retiredRenderObjects_, [&](const auto& objects) {
        if (objects.slotAware) return false;
        return objects.token < completedToken_;
    });
}

} // namespace aeg::render
