#include "TransientTexturePool.hpp"

#include <algorithm>
#include <stdexcept>

namespace aeg::render {

bool TransientTexturePool::compatible(const RenderGraph::TextureDesc& lhs,
                                      const RenderGraph::TextureDesc& rhs) {
    return lhs.extent == rhs.extent && lhs.format == rhs.format && lhs.usage == rhs.usage &&
           lhs.samples == rhs.samples && lhs.mipLevels == rhs.mipLevels && lhs.dimension == rhs.dimension;
}

rhi::Texture& TransientTexturePool::acquire(const RenderGraph::TextureDesc& description) {
    rhi::validate(description);
    for (auto& entry : entries_) {
        if ((!entry.immediatelyAvailable && (!entry.retired || *entry.retired >= completedToken_)) ||
            !compatible(entry.description, description))
            continue;
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
    found->retired = token;
    found->immediatelyAvailable = false;
}

void TransientTexturePool::releaseImmediately(rhi::Texture& texture) {
    const auto found = std::ranges::find_if(entries_, [&](const auto& entry) { return entry.texture.get() == &texture; });
    if (found == entries_.end()) throw std::invalid_argument("Transient pool cannot release an unknown texture");
    found->retired = completedToken_;
    found->immediatelyAvailable = true;
}

void TransientTexturePool::reclaimCompletedFrame(FrameToken token) {
    completedToken_ = std::max(completedToken_, token);
}

} // namespace aeg::render
