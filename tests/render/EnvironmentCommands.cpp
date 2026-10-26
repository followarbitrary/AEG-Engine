#include <Renderer.hpp>
#include <environment/EnvironmentBaker.hpp>
#include "../rhi/fake/FakeDevice.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace {
using namespace aeg;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <class F> void rejects(F action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}
math::Vec4 environmentUniform(const tests::FakeDevice& device) {
    require(device.uniform && device.uniform->bytes.size() == 1264, "Environment uniform ABI must append one vec4");
    math::Vec4 value;
    std::memcpy(&value, device.uniform->bytes.data() + 1248, sizeof(value));
    return value;
}
void enableCompute(tests::FakeDevice& device) {
    device.caps.compute = true;
    device.caps.maximumCubeImageDimension = 4096;
    device.caps.maximumComputeGroupCount = {65535, 65535, 65535};
    device.caps.maximumComputeGroupSize = {1024, 1024, 64};
    device.caps.maximumComputeInvocations = 1024;
}
assets::HdrImageData constantEnvironment() { return {2, 1, {2, .5f, .25f, 1, 2, .5f, .25f, 1}}; }
assets::ModelData plane() {
    assets::ModelData model;
    model.materials.emplace_back();
    auto& mesh = model.meshes.emplace_back();
    mesh.vertices = {{{-1, -1, .5f}, {0, 0, 1}, {}}, {{1, -1, .5f}, {0, 0, 1}, {}}, {{0, 1, .5f}, {0, 0, 1}, {}}};
    mesh.indices = {0, 1, 2};
    return model;
}
std::array<rhi::Texture*, 3> environmentTextures(const tests::FakeCommands& commands) {
    for (const auto& set : commands.recordedPasses.front().sets) {
        if (set.index != 3) continue;
        std::array<rhi::Texture*, 3> result{};
        for (const auto& write : set.writes) result.at(write.binding) = write.texture;
        return result;
    }
    throw std::runtime_error("Missing environment descriptor set");
}
struct BakeConstants {
    std::uint32_t size{}, sampleCount{};
    float roughness{};
    std::uint32_t sourceSize{};
};
static_assert(sizeof(BakeConstants) == 16);
const rhi::BindingWrite& computeBinding(const tests::DispatchSnapshot& dispatch, std::uint32_t index) {
    require(dispatch.sets.size() == 1 && dispatch.sets[0].point == rhi::PipelineBindPoint::Compute &&
                dispatch.sets[0].index == 0 && dispatch.sets[0].layout == dispatch.pipeline.layout,
            "Bake dispatch used the wrong pipeline layout, set or bind point");
    const auto& writes = dispatch.sets[0].writes;
    const auto found = std::ranges::find_if(writes, [&](const auto& write) { return write.binding == index; });
    require(found != writes.end(), "Bake dispatch omitted a shader binding");
    return *found;
}
void bakeDispatchContracts(const tests::FakeDevice& device, const render::EnvironmentMaps& maps, rhi::Texture& lut) {
    require(device.immediateCommands && device.immediateSubmissions == 2,
            "First bake must submit one LUT and one map transaction");
    const auto& commands = *device.immediateCommands;
    require(commands.dispatches.size() == 11 && commands.transitions.size() == 8,
            "Tiny bake omitted LUT, a cube mip, irradiance or a complete-resource transition");
    const auto& dispatches = commands.dispatches;
    for (std::size_t index = 0; index < dispatches.size(); ++index) {
        const auto& dispatch = dispatches[index];
        require(dispatch.constants.size() == 16, "Bake push constants must contain four complete words");
        BakeConstants constants;
        std::memcpy(&constants, dispatch.constants.data(), sizeof(constants));
        const auto& layout = dynamic_cast<tests::FakePipelineLayout&>(*dispatch.pipeline.layout).desc;
        require(layout.pushConstantBytes == 16 && layout.pushConstantStages == rhi::ShaderStage::Compute,
                "Bake constant layout has wrong byte size or shader visibility");
        require(dynamic_cast<tests::FakeShader&>(*dispatch.pipeline.compute).stage == rhi::ShaderStage::Compute,
                "Bake pipeline does not reference a compute shader");
        const auto& target = computeBinding(dispatch, 1);
        require(target.texture && !target.buffer && !target.sampler,
                "Bake storage binding contains a buffer or sampler");
        const auto& bindings = dynamic_cast<tests::FakeBindingLayout&>(*layout.sets.at(0)).desc.bindings;
        for (const auto& binding : bindings)
            require(binding.stages == rhi::ShaderStage::Compute &&
                        binding.type == (binding.binding == 1 ? rhi::BindingType::StorageImage
                                                              : rhi::BindingType::CombinedTextureSampler),
                    "Bake shader binding types or visibility disagree with their resources");
        if (index == 0) {
            require(dispatch.sets[0].writes.size() == 1 && bindings.size() == 1 && target.texture == &lut &&
                        target.storageMipLevel == 0 && constants.size == 16 && constants.sampleCount == 256 &&
                        constants.roughness == 0 && constants.sourceSize == 0 &&
                        dispatch.groups == std::array<std::uint32_t, 3>{2, 2, 1},
                    "BRDF dispatch has wrong output, constants or group count");
            continue;
        }
        require(dispatch.sets[0].writes.size() == 2 && bindings.size() == 2,
                "Cube processing needs distinct sampled and storage bindings");
        const auto& source = computeBinding(dispatch, 0);
        require(source.texture && source.sampler && !source.buffer && source.texture != target.texture,
                "Cube processing samples its storage output or lacks a sampler");
        if (index <= 5) {
            const auto mip = static_cast<std::uint32_t>(index - 1);
            const auto expectedSize = std::array<std::uint32_t, 5>{16, 8, 4, 2, 1}[mip];
            require(target.texture == maps.environment.get() && target.storageMipLevel == mip &&
                        constants.size == expectedSize && constants.sampleCount == (mip == 0 ? 1u : 16u) &&
                        constants.roughness == 0 && constants.sourceSize == 16 &&
                        dispatch.groups ==
                            (mip == 0 ? std::array<std::uint32_t, 3>{2, 2, 6} : std::array<std::uint32_t, 3>{1, 1, 6}),
                    "Environment conversion omitted a face/mip or used the wrong footprint constants");
            const auto& sampler = dynamic_cast<tests::FakeSampler&>(*source.sampler).desc;
            require(sampler.filter == rhi::Filter::Linear && sampler.address == rhi::AddressMode::Repeat &&
                        sampler.addressV == rhi::AddressMode::ClampToEdge && sampler.maxLod == 0,
                    "HDR panorama must wrap longitude and clamp latitude with linear sampling");
        } else {
            require(source.texture == maps.environment.get() && source.storageMipLevel == 0 &&
                        constants.sampleCount == 256 && constants.sourceSize == 16 &&
                        dispatch.groups == std::array<std::uint32_t, 3>{1, 1, 6},
                    "Convolution must sample the completed source cube with the requested sample count");
            const auto& sampler = dynamic_cast<tests::FakeSampler&>(*source.sampler).desc;
            require(sampler.filter == rhi::Filter::Linear && sampler.address == rhi::AddressMode::ClampToEdge &&
                        sampler.maxLod == 4,
                    "Source cube mip sampler has an incorrect LOD range");
            if (index == 6) {
                require(target.texture == maps.irradiance.get() && target.storageMipLevel == 0 && constants.size == 8,
                        "Diffuse irradiance wrote the wrong texture or mip");
            } else {
                const auto mip = static_cast<std::uint32_t>(index - 7);
                const auto expectedRoughness = std::array<float, 4>{.045f, .3633333333f, .6816666667f, 1.0f}[mip];
                require(target.texture == maps.prefiltered.get() && target.storageMipLevel == mip &&
                            constants.size == std::array<std::uint32_t, 4>{8, 4, 2, 1}[mip] &&
                            std::abs(constants.roughness - expectedRoughness) < 1e-6f,
                        "GGX prefilter mip size or perceptual roughness mapping is incorrect");
            }
        }
    }
    require(device.uploadedFormats == std::vector{rhi::Format::Rgba32Float},
            "Panorama upload lost linear float HDR data");
    std::unordered_map<rhi::Texture*, rhi::ResourceState> states;
    std::size_t transitionIndex = 0, dispatchIndex = 0;
    for (const auto& operation : commands.operations) {
        if (operation == "transition") {
            const auto& transition = commands.transitions.at(transitionIndex++);
            require(transition.before == states[transition.texture],
                    "Bake resource transition did not follow its prior state");
            states[transition.texture] = transition.after;
        } else if (operation == "dispatch") {
            const auto& dispatch = dispatches.at(dispatchIndex++);
            const auto& target = computeBinding(dispatch, 1);
            require(states[target.texture] == rhi::ResourceState::Storage,
                    "Bake dispatch wrote before its storage transition");
            if (dispatchIndex > 6)
                require(states[computeBinding(dispatch, 0).texture] == rhi::ResourceState::Sampled,
                        "Convolution read the source cube before all mips became sampled");
        } else {
            throw std::runtime_error("Unexpected operation in environment bake");
        }
    }
    for (auto* texture : {maps.environment.get(), maps.irradiance.get(), maps.prefiltered.get(), &lut})
        require(states[texture] == rhi::ResourceState::Sampled, "Published bake product was left in a storage state");
}
void defaultsAndValidation() {
    tests::FakeDevice device;
    render::Renderer renderer(device);
    require(!renderer.environment().enabled && renderer.environment().intensity == 1 &&
                renderer.environment().rotationRadians == 0 && !renderer.hasEnvironment(),
            "Default IBL must be disabled and lazy");
    require(!renderer.supportsEnvironment(), "Graphics-only device must preserve disabled IBL path");
    tests::FakeCommands commands;
    renderer.recordFrame(commands, {});
    require(environmentUniform(device).x == 0, "Disabled IBL changed the legacy ambient path");
    const auto& sets = commands.recordedPasses.front().sets;
    const auto found = std::ranges::find_if(sets, [](const auto& set) { return set.index == 3; });
    require(found != sets.end() && found->writes.size() == 3,
            "Scene must always bind three valid environment fallbacks");
    for (const auto& write : found->writes)
        require(write.texture && write.sampler, "Environment fallback descriptor is incomplete");
    require(found->writes[0].texture->description().dimension == rhi::TextureDimension::Cube &&
                found->writes[1].texture->description().dimension == rhi::TextureDimension::Cube &&
                found->writes[2].texture->description().dimension == rhi::TextureDimension::Texture2D,
            "Environment fallback view dimensions disagree with shader samplers");
    renderer.frameSubmitted();
    const auto output = renderer.outputTexture();
    const auto original = renderer.environment();
    for (float invalid :
         {-1.0f, 101.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        rejects([&] { renderer.setEnvironment({false, invalid, 0}); }, "Invalid intensity was accepted");
    rejects([&] { renderer.setEnvironment({false, 1, std::numeric_limits<float>::infinity()}); },
            "Invalid rotation was accepted");
    rejects([&] { renderer.setEnvironment({true, 1, 0}); }, "Unsupported IBL was enabled");
    rejects([&] { renderer.uploadEnvironment(assets::makeProceduralEnvironment()); },
            "Unsupported IBL upload was accepted");
    require(renderer.environment() == original && !renderer.hasEnvironment() && renderer.outputTexture() == output,
            "Failed environment changes discarded settings, resources or output");
}
void proceduralSourceCapability() {
    tests::FakeDevice device;
    enableCompute(device);
    device.caps.maximumImageDimension = 256;
    device.caps.maximumCubeImageDimension = 256;
    require(render::EnvironmentBaker::supported(device),
            "Small custom bakes must remain supported on a compute device");
    render::Renderer renderer(device);
    require(!renderer.supportsEnvironment(), "Default procedural HDR needs a 512x256 source texture");
    require(!renderer.hasEnvironment() && device.immediateSubmissions == 0,
            "Unsupported default quality performed preprocessing during renderer setup");
}
void bakerProductsAndValidation() {
    tests::FakeDevice device;
    enableCompute(device);
    require(render::EnvironmentBaker::supported(device), "Compute-capable float sampling device was rejected");
    render::EnvironmentBaker baker(device, {16, 8, 8, 16, 256});
    auto* immutableLut = &baker.brdfLut();
    auto maps = baker.bake(constantEnvironment());
    bakeDispatchContracts(device, maps, *immutableLut);
    require(maps.environment && maps.irradiance && maps.prefiltered && maps.prefilterMipCount == 4,
            "Baker failed to return complete mipmapped products");
    require(maps.environment->description().mipLevels == 5 && maps.irradiance->description().mipLevels == 1 &&
                maps.prefiltered->description().mipLevels == 4,
            "Bake products have incorrect mip chains");
    for (const auto* texture : {maps.environment.get(), maps.irradiance.get(), maps.prefiltered.get(), immutableLut})
        require(texture->description().format == rhi::Format::Rgba32Float &&
                    rhi::contains(texture->description().usage, rhi::TextureUsage::Storage |
                                                                    rhi::TextureUsage::Sampled |
                                                                    rhi::TextureUsage::CopySource),
                "Bake output lost float radiance, storage or diagnostic readback support");
    auto second = baker.bake(constantEnvironment());
    require(&baker.brdfLut() == immutableLut && second.environment.get() != maps.environment.get(),
            "Environment replacement rebuilt immutable LUT or overwrote existing maps");
    for (const auto settings : {render::EnvironmentBakeSettings{3, 1, 1, 1, 64},
                                {16, 8, 32, 16, 256},
                                {16, 8, 8, 16, 63},
                                {16, 8, 8, 512, 256}})
        rejects([&] { render::EnvironmentBaker invalid(device, settings); }, "Invalid bake settings were accepted");
    for (float invalid :
         {-1.0f, 65505.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        auto image = constantEnvironment();
        image.rgba[0] = invalid;
        rejects([&] { (void)baker.bake(image); }, "Invalid HDR radiance was accepted");
    }
    auto image = constantEnvironment();
    image.rgba[3] = 0;
    rejects([&] { (void)baker.bake(image); }, "Nonopaque HDR alpha was accepted");
    image = constantEnvironment();
    image.rgba.pop_back();
    rejects([&] { (void)baker.bake(image); }, "Truncated HDR pixel buffer was accepted");
    image = constantEnvironment();
    image.width = 1;
    rejects([&] { (void)baker.bake(image); }, "Nonequirectangular HDR dimensions were accepted");
}
void publicationAndHistory() {
    tests::FakeDevice device;
    enableCompute(device);
    render::Renderer renderer(device);
    renderer.uploadModel(plane());
    renderer.setAntialiasing({1, false, true});
    auto frame = [&] {
        const auto immediateSubmissions = device.immediateSubmissions;
        tests::FakeCommands commands;
        renderer.recordFrame(commands, {});
        renderer.frameSubmitted();
        require(device.immediateSubmissions == immediateSubmissions,
                "Ordinary frame recording dispatched environment preprocessing");
        return environmentTextures(commands);
    };
    const auto fallback = frame();
    frame();
    require(renderer.temporalFrameCount() == 2, "Fixture failed to accumulate TAA");
    renderer.uploadEnvironment(constantEnvironment());
    require(renderer.hasEnvironment() && !renderer.environment().enabled && renderer.temporalFrameCount() == 0,
            "Upload must publish ready maps and reset history without enabling IBL");
    renderer.setEnvironment({true, 2, .5f});
    const auto active = frame();
    const auto value = environmentUniform(device);
    require(value.x == 1 && value.y == 2 && value.z == .5f && value.w == 7,
            "Environment controls or prefilter max LOD were packed incorrectly");
    require(active[0] != fallback[0] && active[1] != fallback[1] && active[2] != fallback[2],
            "Enabled IBL still samples fallback products");
    const auto submittedBeforeFrames = device.immediateSubmissions;
    renderer.setEnvironment(renderer.environment());
    require(renderer.temporalFrameCount() == 1, "Identical environment settings reset TAA");
    require(device.immediateSubmissions == submittedBeforeFrames,
            "Unchanged environment settings dispatched preprocessing");
    for (unsigned failure = 0; failure < 4; ++failure) {
        const auto output = renderer.outputTexture();
        if (failure == 0) device.failTexture = true;
        if (failure == 1) device.failBindingSet = true;
        if (failure == 2) device.failWaitIdle = true;
        if (failure == 3) device.failImmediateSubmit = true;
        rejects([&] { renderer.uploadEnvironment(constantEnvironment()); },
                "Injected environment replacement failure was ignored");
        require(renderer.outputTexture() == output && renderer.temporalFrameCount() == 1 &&
                    renderer.environment() == render::EnvironmentSettings{true, 2, .5f} && renderer.hasEnvironment(),
                "Failed environment replacement changed output, history or settings");
        tests::FakeCommands unchanged;
        renderer.recordFrame(unchanged, {});
        require(environmentTextures(unchanged) == active, "Failed replacement changed visible environment resources");
    }
    renderer.uploadEnvironment(constantEnvironment());
    const auto replaced = frame();
    require(replaced[0] != active[0] && replaced[1] != active[1] && replaced[2] == active[2],
            "Replacement must publish new cube maps and retain immutable BRDF LUT");
    renderer.setEnvironment({false, 2, .5f});
    require(renderer.temporalFrameCount() == 0 && renderer.hasEnvironment(),
            "Disabling IBL must retain ready environment and reset TAA");
    tests::FakeCommands disabled;
    renderer.recordFrame(disabled, {});
    require(environmentUniform(device).x == 0, "Disabled IBL uniform remains enabled");
    tests::FakeDevice lazyDevice;
    enableCompute(lazyDevice);
    render::Renderer lazy(lazyDevice);
    for (unsigned failure = 0; failure < 5; ++failure) {
        if (failure == 0) lazyDevice.failTexture = true;
        if (failure == 1) lazyDevice.failBindingSet = true;
        if (failure == 2) lazyDevice.failComputePipeline = true;
        if (failure == 3) lazyDevice.failImmediateSubmit = true;
        if (failure == 4) lazyDevice.failWaitIdle = true;
        rejects([&] { lazy.setEnvironment({true, 1, 0}); }, "Initial procedural bake failure was ignored");
        require(!lazy.hasEnvironment() && !lazy.environment().enabled, "Failed lazy bake published partial state");
    }
    lazy.setEnvironment({true, 1, 0});
    require(lazy.hasEnvironment() && lazy.environment().enabled, "First enable did not prepare procedural environment");
}
}
int main() {
    try {
        defaultsAndValidation();
        proceduralSourceCapability();
        bakerProductsAndValidation();
        publicationAndHistory();
        std::cout << "PASS environment defaults, products, validation, publication, rollback and history\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL environment commands: " << error.what() << '\n';
        return 1;
    }
}
