#pragma once
#include <RhiTypes.hpp>
#include <vector>
namespace aeg::rhi {
struct TextureDesc {
    Extent2D extent;
    Format format{Format::Rgba8Unorm};
    TextureUsage usage{TextureUsage::Sampled};
    std::uint32_t samples{1};
};
void validate(const TextureDesc& desc);
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
