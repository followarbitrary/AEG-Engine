#pragma once
#include <RhiTypes.hpp>
#include <vector>
namespace aeg::rhi {
struct TextureDesc {
    Extent2D extent;
    Format format{Format::Rgba8Unorm};
    TextureUsage usage{TextureUsage::Sampled};
    std::uint32_t samples{1};
    std::uint32_t mipLevels{1};
    TextureDimension dimension{TextureDimension::Texture2D};
};
void validate(const TextureDesc& desc);
struct TextureSubresource {
    std::uint32_t mipLevel{}, layer{};
};
[[nodiscard]] Extent2D mipExtent(const TextureDesc& desc, std::uint32_t mip);
[[nodiscard]] std::uint32_t textureLayerCount(const TextureDesc& desc);
class Texture {
public:
    virtual ~Texture() = default;
    [[nodiscard]] virtual const TextureDesc& description() const = 0;
};
struct TextureReadback {
    Extent2D extent;
    Format format{Format::Undefined};
    std::vector<std::byte> pixels;
};
}
