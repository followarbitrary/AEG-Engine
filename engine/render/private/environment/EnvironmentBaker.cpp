#include "EnvironmentBaker.hpp"
#include "shaders/ShaderLibrary.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aeg::render {
namespace {
constexpr auto outputUsage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySource;
struct BakePush {
    std::uint32_t size{}, sampleCount{};
    float roughness{};
    std::uint32_t sourceSize{};
};
static_assert(sizeof(BakePush) == 16);
bool floatSupport(rhi::Device& device, rhi::TextureDimension dimension) {
    const auto support = device.textureSupport(rhi::Format::Rgba32Float, outputUsage, dimension);
    return support.supported && (support.sampleMask & 1u) && support.linearFiltering && support.maximumExtent.width &&
           support.maximumExtent.height;
}
void validateSettings(rhi::Device& device, const EnvironmentBakeSettings& settings) {
    const auto validSize = [](std::uint32_t value, std::uint32_t maximum) {
        return value && value <= maximum && std::has_single_bit(value);
    };
    if (!validSize(settings.environmentSize, 512) || !validSize(settings.irradianceSize, 128) ||
        !validSize(settings.prefilterSize, 512) || !validSize(settings.brdfSize, 256) ||
        settings.prefilterSize > settings.environmentSize || settings.sampleCount < 64 || settings.sampleCount > 1024)
        throw std::invalid_argument("Invalid environment bake size or sample count");
    if (!EnvironmentBaker::supported(device)) throw std::runtime_error("Compute environment baking is unsupported");
    const auto& caps = device.capabilities();
    for (const auto size :
         {settings.environmentSize, settings.irradianceSize, settings.prefilterSize, settings.brdfSize}) {
        if ((size + 7) / 8 > caps.maximumComputeGroupCount[0] || (size + 7) / 8 > caps.maximumComputeGroupCount[1])
            throw std::invalid_argument("Environment dispatch exceeds device limits");
    }
    const auto cube = device.textureSupport(rhi::Format::Rgba32Float, outputUsage, rhi::TextureDimension::Cube);
    const auto flat = device.textureSupport(rhi::Format::Rgba32Float, outputUsage);
    const auto maximumCube =
        std::min({caps.maximumCubeImageDimension, cube.maximumExtent.width, cube.maximumExtent.height});
    if (std::max({settings.environmentSize, settings.irradianceSize, settings.prefilterSize}) > maximumCube ||
        settings.brdfSize > std::min({caps.maximumImageDimension, flat.maximumExtent.width, flat.maximumExtent.height}))
        throw std::invalid_argument("Environment texture size exceeds device limits");
}
void validateImage(rhi::Device& device, const assets::HdrImageData& image) {
    const auto support = device.textureSupport(rhi::Format::Rgba32Float,
                                               rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDestination);
    const auto& caps = device.capabilities();
    if (!support.supported || !support.linearFiltering || !(support.sampleMask & 1u) || !image.height ||
        std::uint64_t(image.width) != std::uint64_t(image.height) * 2 || image.width > caps.maximumImageDimension ||
        image.height > caps.maximumImageDimension || image.width > support.maximumExtent.width ||
        image.height > support.maximumExtent.height ||
        std::uint64_t(image.width) * image.height > std::numeric_limits<std::size_t>::max() / 4 ||
        std::size_t(image.width) * image.height * 4 != image.rgba.size())
        throw std::invalid_argument("Invalid or oversized HDR equirectangular image");
    for (std::size_t index = 0; index < image.rgba.size(); index += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const float value = image.rgba[index + channel];
            if (!std::isfinite(value) || value < 0 || value > 65504)
                throw std::invalid_argument("HDR radiance must be finite and in [0,65504]");
        }
        if (image.rgba[index + 3] != 1) throw std::invalid_argument("HDR alpha must be one");
    }
}
struct ComputeProgram {
    std::unique_ptr<rhi::BindingLayout> bindings;
    std::unique_ptr<rhi::PipelineLayout> layout;
    std::unique_ptr<rhi::Shader> shader;
    std::unique_ptr<rhi::ComputePipeline> pipeline;
};
ComputeProgram makeProgram(rhi::Device& device, detail::ShaderId id, bool source) {
    ComputeProgram result;
    rhi::BindingLayoutDesc bindings;
    if (source) bindings.bindings.push_back({0, rhi::BindingType::CombinedTextureSampler, rhi::ShaderStage::Compute});
    bindings.bindings.push_back({1, rhi::BindingType::StorageImage, rhi::ShaderStage::Compute});
    result.bindings = device.createBindingLayout(bindings);
    result.layout = device.createPipelineLayout({{result.bindings.get()}, sizeof(BakePush), rhi::ShaderStage::Compute});
    result.shader =
        device.createShader({rhi::ShaderStage::Compute, rhi::ShaderFormat::Spirv, detail::embeddedShader(id)});
    result.pipeline = device.createComputePipeline({result.layout.get(), result.shader.get()});
    return result;
}
std::unique_ptr<rhi::Texture> makeOutput(rhi::Device& device, std::uint32_t size, rhi::TextureDimension dimension,
                                         std::uint32_t mips = 1) {
    return device.createTexture({{size, size}, rhi::Format::Rgba32Float, outputUsage, 1, mips, dimension});
}
std::unique_ptr<rhi::BindingSet> makeSet(rhi::Device& device, ComputeProgram& program, rhi::Texture& output,
                                         std::uint32_t mip = 0, rhi::Texture* source = nullptr,
                                         rhi::Sampler* sampler = nullptr) {
    std::vector<rhi::BindingWrite> writes;
    if (source) writes.push_back({0, nullptr, source, sampler});
    writes.push_back({1, nullptr, &output, nullptr, mip});
    return device.createBindingSet(*program.bindings, writes);
}
void dispatch(rhi::CommandList& commands, ComputeProgram& program, rhi::BindingSet& set, const BakePush& push,
              std::uint32_t faces) {
    commands.bindComputePipeline(*program.pipeline);
    commands.bindSet(*program.layout, 0, set, rhi::PipelineBindPoint::Compute);
    commands.pushConstants(*program.layout, std::as_bytes(std::span(&push, 1)));
    commands.dispatch((push.size + 7) / 8, (push.size + 7) / 8, faces);
}
}

struct EnvironmentBaker::Impl {
    rhi::Device& device;
    EnvironmentBakeSettings settings;
    ComputeProgram conversion, diffuse, prefilter, brdf;
    std::unique_ptr<rhi::Sampler> equirectangularSampler, cubeSampler;
    std::unique_ptr<rhi::Texture> lut;
    Impl(rhi::Device& target, const EnvironmentBakeSettings& requested) : device(target), settings(requested) {
        validateSettings(device, settings);
        conversion = makeProgram(device, detail::ShaderId::EnvironmentCompute, true);
        diffuse = makeProgram(device, detail::ShaderId::IrradianceCompute, true);
        prefilter = makeProgram(device, detail::ShaderId::PrefilterCompute, true);
        brdf = makeProgram(device, detail::ShaderId::BrdfCompute, false);
        equirectangularSampler =
            device.createSampler({rhi::Filter::Linear, rhi::AddressMode::Repeat, rhi::AddressMode::ClampToEdge, 0});
        cubeSampler = device.createSampler({rhi::Filter::Linear,
                                            rhi::AddressMode::ClampToEdge,
                                            {},
                                            float(std::bit_width(settings.environmentSize) - 1)});
        lut = makeOutput(device, settings.brdfSize, rhi::TextureDimension::Texture2D);
        auto descriptor = makeSet(device, brdf, *lut);
        device.submitImmediate([&](rhi::CommandList& commands) {
            commands.transition(*lut, rhi::ResourceState::Undefined, rhi::ResourceState::Storage);
            dispatch(commands, brdf, *descriptor, {settings.brdfSize, settings.sampleCount, 0, 0}, 1);
            commands.transition(*lut, rhi::ResourceState::Storage, rhi::ResourceState::Sampled);
        });
    }
};

EnvironmentBaker::EnvironmentBaker(rhi::Device& device, const EnvironmentBakeSettings& settings)
    : impl_(std::make_unique<Impl>(device, settings)) {}
EnvironmentBaker::~EnvironmentBaker() = default;
bool EnvironmentBaker::supported(rhi::Device& device) {
    const auto& caps = device.capabilities();
    if (!caps.compute || !caps.maximumCubeImageDimension || caps.maximumComputeGroupSize[0] < 8 ||
        caps.maximumComputeGroupSize[1] < 8 || caps.maximumComputeGroupSize[2] < 1 ||
        caps.maximumComputeInvocations < 64 || !caps.maximumComputeGroupCount[0] || !caps.maximumComputeGroupCount[1] ||
        caps.maximumComputeGroupCount[2] < 6)
        return false;
    return floatSupport(device, rhi::TextureDimension::Cube) && floatSupport(device, rhi::TextureDimension::Texture2D);
}
rhi::Texture& EnvironmentBaker::brdfLut() const { return *impl_->lut; }

EnvironmentMaps EnvironmentBaker::bake(const assets::HdrImageData& image) {
    auto& self = *impl_;
    validateImage(self.device, image);
    auto source = self.device.uploadTexture({{image.width, image.height},
                                             rhi::Format::Rgba32Float,
                                             rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDestination,
                                             1},
                                            std::as_bytes(std::span(image.rgba)));
    EnvironmentMaps result;
    const auto environmentMips = std::bit_width(self.settings.environmentSize);
    result.prefilterMipCount = std::bit_width(self.settings.prefilterSize);
    result.environment =
        makeOutput(self.device, self.settings.environmentSize, rhi::TextureDimension::Cube, environmentMips);
    result.irradiance = makeOutput(self.device, self.settings.irradianceSize, rhi::TextureDimension::Cube);
    result.prefiltered =
        makeOutput(self.device, self.settings.prefilterSize, rhi::TextureDimension::Cube, result.prefilterMipCount);
    std::vector<std::unique_ptr<rhi::BindingSet>> conversionSets, prefilterSets;
    for (std::uint32_t mip = 0; mip < environmentMips; ++mip)
        conversionSets.push_back(makeSet(self.device, self.conversion, *result.environment, mip, source.get(),
                                         self.equirectangularSampler.get()));
    auto diffuseSet =
        makeSet(self.device, self.diffuse, *result.irradiance, 0, result.environment.get(), self.cubeSampler.get());
    for (std::uint32_t mip = 0; mip < result.prefilterMipCount; ++mip)
        prefilterSets.push_back(makeSet(self.device, self.prefilter, *result.prefiltered, mip, result.environment.get(),
                                        self.cubeSampler.get()));
    // Every immutable descriptor, sampler and input remains alive through synchronous submission.
    self.device.submitImmediate([&](rhi::CommandList& commands) {
        commands.transition(*result.environment, rhi::ResourceState::Undefined, rhi::ResourceState::Storage);
        for (std::uint32_t mip = 0; mip < environmentMips; ++mip)
            dispatch(commands, self.conversion, *conversionSets[mip],
                     {self.settings.environmentSize >> mip, mip == 0 ? 1u : 16u, 0, self.settings.environmentSize}, 6);
        commands.transition(*result.environment, rhi::ResourceState::Storage, rhi::ResourceState::Sampled);
        commands.transition(*result.irradiance, rhi::ResourceState::Undefined, rhi::ResourceState::Storage);
        dispatch(commands, self.diffuse, *diffuseSet,
                 {self.settings.irradianceSize, self.settings.sampleCount, 0, self.settings.environmentSize}, 6);
        commands.transition(*result.irradiance, rhi::ResourceState::Storage, rhi::ResourceState::Sampled);
        commands.transition(*result.prefiltered, rhi::ResourceState::Undefined, rhi::ResourceState::Storage);
        for (std::uint32_t mip = 0; mip < result.prefilterMipCount; ++mip) {
            const float roughness = result.prefilterMipCount == 1
                                        ? 0.045f
                                        : 0.045f + 0.955f * float(mip) / float(result.prefilterMipCount - 1);
            dispatch(commands, self.prefilter, *prefilterSets[mip],
                     {self.settings.prefilterSize >> mip, self.settings.sampleCount, roughness,
                      self.settings.environmentSize},
                     6);
        }
        commands.transition(*result.prefiltered, rhi::ResourceState::Storage, rhi::ResourceState::Sampled);
    });
    return result;
}
}
