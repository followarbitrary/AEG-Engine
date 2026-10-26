#pragma once
#include <RhiTypes.hpp>
namespace aeg::rhi {
struct SamplerDesc {
    Filter filter{Filter::Linear};
    AddressMode address{AddressMode::Repeat};
};
class Sampler {
public:
    virtual ~Sampler() = default;
};
}
