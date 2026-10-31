#include <RhiBuffer.hpp>
#include <RhiPipeline.hpp>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <bit>
#include <cmath>

namespace aeg::rhi {
Extent2D mipExtent(const TextureDesc& desc, std::uint32_t mip) {
    if (!desc.extent.width || !desc.extent.height || mip >= desc.mipLevels || mip >= 32)
        throw std::invalid_argument("Texture mip is out of range");
    return {std::max(1u, desc.extent.width >> mip), std::max(1u, desc.extent.height >> mip)};
}
std::uint32_t textureLayerCount(const TextureDesc& desc) {
    if (desc.dimension == TextureDimension::Cube) return 6;
    if (desc.dimension == TextureDimension::Texture2D) return 1;
    throw std::invalid_argument("Unknown texture dimension");
}
void validate(const SamplerDesc& desc) {
    const auto validAddress = [](AddressMode value) {
        return value == AddressMode::Repeat || value == AddressMode::ClampToEdge;
    };
    if ((desc.filter != Filter::Linear && desc.filter != Filter::Nearest) || !validAddress(desc.address) ||
        (desc.addressV && !validAddress(*desc.addressV)) || !std::isfinite(desc.maxLod) || desc.maxLod < 0 ||
        !std::isfinite(desc.maxAnisotropy) || desc.maxAnisotropy < 1.0f)
        throw std::invalid_argument("Invalid sampler filtering, addressing or mip range");
}
void validate(const ComputePipelineDesc& desc) {
    if (!desc.layout || !desc.compute) throw std::invalid_argument("Missing compute pipeline resource");
}
namespace {
void samples(std::uint32_t value) {
    if (value != 1 && value != 2 && value != 4 && value != 8)
        throw std::invalid_argument("Sample count must be 1, 2, 4 or 8");
}
}
std::uint32_t formatByteSize(Format format) {
    switch (format) {
    case Format::Rgba8Unorm:
    case Format::Rgba8Srgb:
    case Format::Bgra8Unorm:
    case Format::Bgra8Srgb:
    case Format::R32Float:
    case Format::D32Float:
    case Format::D24UnormS8:
        return 4;
    case Format::Rgba16Float:
        return 8;
    case Format::Rgba32Float:
        return 16;
    case Format::D16Unorm:
        return 2;
    default:
        throw std::invalid_argument("Undefined texture format");
    }
}
void validate(const BufferDesc& desc) {
    constexpr auto valid = std::to_underlying(BufferUsage::Vertex | BufferUsage::Index | BufferUsage::Uniform |
                                              BufferUsage::CopySource | BufferUsage::CopyDestination |
                                              BufferUsage::Storage | BufferUsage::Indirect);
    if (desc.size == 0 || desc.usage == BufferUsage::None || (std::to_underlying(desc.usage) & ~valid))
        throw std::invalid_argument("Buffer requires nonzero size and valid usage");
}
void validate(const TextureDesc& desc) {
    samples(desc.samples);
    (void)formatByteSize(desc.format);
    constexpr auto valid =
        std::to_underlying(TextureUsage::Sampled | TextureUsage::ColorAttachment | TextureUsage::DepthAttachment |
                           TextureUsage::CopySource | TextureUsage::CopyDestination | TextureUsage::Storage);
    if (!desc.extent.width || !desc.extent.height || desc.usage == TextureUsage::None ||
        (std::to_underlying(desc.usage) & ~valid))
        throw std::invalid_argument("Texture requires nonzero extent and valid usage");
    (void)textureLayerCount(desc);
    if (!desc.mipLevels || desc.mipLevels > std::bit_width(std::max(desc.extent.width, desc.extent.height)) ||
        (desc.samples != 1 && desc.mipLevels != 1))
        throw std::invalid_argument("Invalid texture mip count");
    if (desc.dimension == TextureDimension::Cube && (desc.extent.width != desc.extent.height || desc.samples != 1))
        throw std::invalid_argument("Cube texture requires square single-sample faces");
    if (contains(desc.usage, TextureUsage::Storage) && (isDepthFormat(desc.format) || desc.samples != 1))
        throw std::invalid_argument("Storage textures require a single-sample color format");
    if (isDepthFormat(desc.format) && contains(desc.usage, TextureUsage::ColorAttachment))
        throw std::invalid_argument("Depth format cannot be a color attachment");
    if (!isDepthFormat(desc.format) && contains(desc.usage, TextureUsage::DepthAttachment))
        throw std::invalid_argument("Color format cannot be a depth attachment");
}
void validate(const RenderPassDesc& desc) {
    if (desc.attachments.empty() || (desc.colors.empty() && !desc.depth) ||
        (!desc.resolves.empty() && desc.resolves.size() != desc.colors.size()))
        throw std::invalid_argument("Invalid render pass attachment counts");
    std::set<std::uint32_t> used;
    const auto claim = [&](std::uint32_t index, bool depth) -> const AttachmentDesc& {
        if (index >= desc.attachments.size() || !used.insert(index).second)
            throw std::invalid_argument("Invalid or overlapping render pass attachment");
        const auto& attachment = desc.attachments[index];
        samples(attachment.samples);
        (void)formatByteSize(attachment.format);
        if (isDepthFormat(attachment.format) != depth ||
            (attachment.load == LoadOp::Load && attachment.initialState == ResourceState::Undefined))
            throw std::invalid_argument("Incompatible render pass attachment");
        return attachment;
    };
    std::uint32_t colorSamples{};
    for (auto index : desc.colors) {
        const auto& color = claim(index, false);
        if (colorSamples && colorSamples != color.samples)
            throw std::invalid_argument("Mismatched color sample counts");
        colorSamples = color.samples;
    }
    if (desc.depth) {
        const auto depthSamples = claim(*desc.depth, true).samples;
        if (colorSamples && depthSamples != colorSamples) throw std::invalid_argument("Mismatched depth sample count");
    }
    for (std::size_t i = 0; i < desc.resolves.size(); ++i) {
        if (desc.resolves[i] == unusedAttachment) continue;
        const auto& resolve = claim(desc.resolves[i], false);
        if (colorSamples == 1 || resolve.samples != 1 || resolve.format != desc.attachments[desc.colors[i]].format)
            throw std::invalid_argument("Invalid multisample resolve attachment");
    }
}
void validate(const GraphicsPipelineDesc& desc, bool independentBlend) {
    samples(desc.samples);
    if (!desc.colorBlends.empty() && desc.colorBlends.size() != desc.colorAttachmentCount)
        throw std::invalid_argument("Pipeline blend states must match the color attachment count");
    for (const auto mode : desc.colorBlends)
        if (mode != BlendMode::Opaque && mode != BlendMode::Alpha)
            throw std::invalid_argument("Invalid color blend mode");
    if (!independentBlend && !desc.colorBlends.empty())
        for (const auto mode : desc.colorBlends)
            if (mode != desc.colorBlends.front())
                throw std::invalid_argument("Different attachment blending requires independent blend support");
}
}
