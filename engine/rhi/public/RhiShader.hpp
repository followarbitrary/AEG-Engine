#pragma once
#include <RhiTypes.hpp>
#include <span>
namespace aeg::rhi {
enum class ShaderFormat { Spirv };
struct ShaderDesc {
    ShaderStage stage{ShaderStage::Vertex};
    ShaderFormat format{ShaderFormat::Spirv};
    std::span<const std::byte> bytecode;
};
class Shader {
public:
    virtual ~Shader() = default;
};
}
