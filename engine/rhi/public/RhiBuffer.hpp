#pragma once
#include <RhiTypes.hpp>
#include <span>
namespace aeg::rhi {
struct BufferDesc {
    std::size_t size{};
    BufferUsage usage{BufferUsage::None};
    MemoryUsage memory{MemoryUsage::Device};
};
void validate(const BufferDesc& desc);
class Buffer {
public:
    virtual ~Buffer() = default;
    [[nodiscard]] virtual const BufferDesc& description() const = 0;
    [[nodiscard]] virtual std::span<std::byte> mapped() = 0;
};
}
