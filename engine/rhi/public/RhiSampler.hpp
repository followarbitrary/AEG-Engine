#pragma once
#include <RhiTypes.hpp>
#include <optional>
namespace aeg::rhi {
struct SamplerDesc {
    Filter filter{Filter::Linear};
    AddressMode address{AddressMode::Repeat};
    std::optional<AddressMode> addressV{};
    float maxLod{0};
};
void validate(const SamplerDesc& desc);
class Sampler {
public:
    virtual ~Sampler() = default;
};
}
