#pragma once
#include <RhiBuffer.hpp>
#include <RhiTexture.hpp>
#include <RhiSampler.hpp>
#include <span>
#include <vector>
namespace aeg::rhi {
enum class BindingType { UniformBuffer, CombinedTextureSampler, StorageImage, StorageBuffer };
struct BindingDesc {
    std::uint32_t binding{};
    BindingType type{BindingType::UniformBuffer};
    ShaderStage stages{ShaderStage::Fragment};
    std::uint32_t arraySize{1};
};
struct BindingLayoutDesc {
    std::vector<BindingDesc> bindings;
};
class BindingLayout {
public:
    virtual ~BindingLayout() = default;
};
struct BindingWrite {
    std::uint32_t binding{};
    Buffer* buffer{};
    Texture* texture{};
    Sampler* sampler{};
    std::uint32_t storageMipLevel{};
    std::uint32_t arrayElement{};
};
class BindingSet {
public:
    virtual ~BindingSet() = default;
    virtual void update(std::span<const BindingWrite> writes) = 0;
};
}
