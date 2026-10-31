#pragma once

#include <RhiTypes.hpp>
#include <cstddef>
#include <memory>
#include <span>

namespace aeg::rhi {
class Buffer;
class Texture;
struct TextureDesc;

class UploadBatch {
public:
    virtual ~UploadBatch() = default;
    virtual std::unique_ptr<Buffer> uploadBuffer(std::span<const std::byte> bytes, BufferUsage usage) = 0;
    virtual std::unique_ptr<Texture> uploadTexture(const TextureDesc& desc,
                                                   std::span<const std::byte> pixels) = 0;
    virtual void end() = 0;
    virtual void cancel() noexcept = 0;
};
}
