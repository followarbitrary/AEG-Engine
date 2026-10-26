#include <Renderer.hpp>
#include <ShadowSettings.hpp>
#include "../rhi/fake/FakeDevice.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace aeg;
void require(bool condition, const std::string& message) {
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
template <class T> T uploaded(const tests::FakeDevice& device, std::size_t offset) {
    require(device.uniform && offset + sizeof(T) <= device.uniform->bytes.size(), "Uniform ABI read outside buffer");
    T result;
    std::memcpy(&result, device.uniform->bytes.data() + offset, sizeof(T));
    return result;
}
const tests::PassSnapshot& named(const tests::FakeCommands& commands, const std::string& name) {
    const auto found = std::ranges::find(commands.passes, name);
    require(found != commands.passes.end(), "Missing " + name + " pass");
    return commands.recordedPasses.at(std::size_t(found - commands.passes.begin()));
}
const tests::BoundSetSnapshot& bound(const tests::PassSnapshot& pass, std::uint32_t index) {
    const auto found = std::ranges::find_if(pass.sets, [=](const auto& set) { return set.index == index; });
    require(found != pass.sets.end(), "Missing descriptor set " + std::to_string(index));
    return *found;
}
const rhi::BindingWrite& binding(const tests::BoundSetSnapshot& set, std::uint32_t index) {
    const auto found = std::ranges::find_if(set.writes, [=](const auto& write) { return write.binding == index; });
    require(found != set.writes.end(), "Missing descriptor binding");
    return *found;
}
assets::ModelData fixture() {
    assets::ModelData model;
    model.name = "Shadow policy with procedural bounds";
    for (unsigned i = 0; i < 3; ++i) {
        auto& material = model.materials.emplace_back();
        material.alphaMode = i == 0   ? assets::AlphaMode::Opaque
                             : i == 1 ? assets::AlphaMode::Mask
                                      : assets::AlphaMode::Blend;
        material.baseColor = {0.3f + float(i) * 0.1f, 0.5f, 0.7f, 0.65f};
        material.alphaCutoff = 0.37f;
        auto& mesh = model.meshes.emplace_back();
        const float x = float(i) * 2 - 2;
        mesh.vertices = {{{x - 0.4f, -0.5f, -3}, {0, 0, 1}, {0, 0}},
                         {{x + 0.4f, -0.5f, -3}, {0, 0, 1}, {1, 0}},
                         {{x, 0.5f, -2}, {0, 0, 1}, {0.5f, 1}}};
        mesh.indices = {0, 1, 2};
        mesh.materialIndex = i;
    }
    // Leave ModelData.bounds default: rendering must derive bounds from vertices.
    return model;
}
render::RenderSettings settings() {
    render::RenderSettings result;
    result.lights[0].direction = {0.25f, -0.5f, -1};
    result.set_camera({math::identity(), math::perspective(1, 1, 0.1f, 100, {true, false}), {true, false}});
    return result;
}
void resourcesAndPolicy() {
    tests::FakeDevice device;
    render::Renderer renderer(device);
    require(renderer.supportsShadows() && renderer.maximumShadowResolution() == 4096,
            "Sampled-depth shadow support missing");
    require(!renderer.shadows().enabled, "Shadows must default disabled");
    renderer.resizeViewport(37, 29);
    renderer.uploadModel(fixture());
    auto sceneSettings = settings();
    tests::FakeCommands off;
    renderer.recordFrame(off, sceneSettings);
    require(off.passes == std::vector<std::string>{"scene"}, "Disabled shadows emitted a depth pass");
    const auto& fallback = binding(bound(named(off, "scene"), 2), 0);
    require(fallback.texture && fallback.sampler && !fallback.buffer &&
                fallback.texture->description().format == rhi::Format::Rgba8Unorm &&
                fallback.texture->description().extent == rhi::Extent2D{1, 1} &&
                rhi::contains(fallback.texture->description().usage, rhi::TextureUsage::Sampled),
            "Disabled scene lacks a valid white shadow fallback binding");
    require(dynamic_cast<tests::FakeTexture&>(*fallback.texture).uploaded == std::vector<std::byte>(4, std::byte{255}),
            "Fallback shadow texel is not initialized white");
    require(uploaded<math::Vec4>(device, 992).x == 0, "Disabled shadow uniform is enabled");
    renderer.frameSubmitted();
    renderer.setAntialiasing({1, true, true});
    render::ShadowSettings shadow;
    shadow.enabled = true;
    shadow.resolution = 128;
    renderer.setShadows(shadow);
    tests::FakeCommands on;
    renderer.recordFrame(on, sceneSettings);
    require(on.passes == std::vector<std::string>{"shadow", "scene", "taa", "fxaa"},
            "Shadow/scene/TAA/FXAA ordering changed");
    const auto& depth = named(on, "shadow");
    require(depth.description.colors.empty() && depth.description.depth == 0u && depth.description.resolves.empty() &&
                depth.description.attachments.size() == 1 && depth.framebuffer.attachments.size() == 1 &&
                depth.framebuffer.extent == rhi::Extent2D{128, 128},
            "Shadow pass is not a single depth-only target");
    const auto& texture = depth.attachmentDescriptions.at(0);
    require(texture.format == rhi::Format::D32Float && texture.samples == 1 &&
                rhi::contains(texture.usage, rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled) &&
                !rhi::contains(texture.usage, rhi::TextureUsage::ColorAttachment),
            "Shadow map is not true sampled depth");
    const auto& attachment = depth.description.attachments.at(0);
    require(attachment.load == rhi::LoadOp::Clear && attachment.store == rhi::StoreOp::Store &&
                attachment.finalState == rhi::ResourceState::Sampled && depth.clears.size() == 1 &&
                depth.clears[0].depth == 1,
            "Shadow depth is not cleared/stored for later sampling");
    require(depth.pipeline.colorAttachmentCount == 0 && depth.pipeline.samples == 1 && depth.pipeline.depthTest &&
                depth.pipeline.depthWrite && depth.pipeline.depthCompare == rhi::DepthCompare::Less &&
                depth.pipeline.colorBlends.empty(),
            "Shadow pipeline does not use canonical depth-only Less testing");
    const auto& scene = named(on, "scene");
    const auto& sampled = binding(bound(scene, 2), 0);
    require(sampled.texture == depth.framebuffer.attachments[0] && sampled.sampler,
            "Scene descriptor does not sample the actual rendered shadow texture");
    const auto& sampler = dynamic_cast<tests::FakeSampler&>(*sampled.sampler).desc;
    require(sampler.filter == rhi::Filter::Nearest, "Manual PCF requires nearest depth samples");
    require(binding(bound(depth, 0), 0).buffer == device.uniform &&
                binding(bound(scene, 0), 0).buffer == device.uniform,
            "Shadow and scene do not share the scene uniform buffer");
    require(depth.draws.size() == 2 && scene.draws.size() == 3, "Opaque/Mask cast and Blend receives policy changed");
    for (const auto& draw : depth.draws) {
        require(draw.constants.size() == 48, "Shadow material push ABI changed");
        const auto counterpart =
            std::ranges::find_if(scene.draws, [&](const auto& value) { return value.indices == draw.indices; });
        require(counterpart != scene.draws.end() && counterpart->constants == draw.constants,
                "Shadow caster does not use the scene material alpha/cutoff constants");
        std::uint32_t mode{};
        float cutoff{};
        std::memcpy(&mode, draw.constants.data() + 20, sizeof(mode));
        std::memcpy(&cutoff, draw.constants.data() + 16, sizeof(cutoff));
        require(mode != std::uint32_t(assets::AlphaMode::Blend) && cutoff == 0.37f,
                "Blend cast or Mask cutoff changed");
    }
    require(device.uniform->bytes.size() == 1024, "Shared scene uniform must be 1024 bytes");
    require(uploaded<math::Vec4>(device, 992) == math::Vec4(1, 0, 1.0f / 128, 0) &&
                uploaded<math::Vec4>(device, 1008).x == shadow.depthBias &&
                uploaded<math::Vec4>(device, 1008).y == shadow.slopeBias,
            "Shadow metadata ABI changed");
    renderer.frameSubmitted();
    // Shadow clip convention stays canonical even when the scene changes axes,
    // projection, reversed Z, model translation, rotation and nonuniform scale.
    for (const bool perspective : {false, true})
        for (const bool down : {false, true})
            for (const bool reversed : {false, true}) {
                const math::ClipSpaceConvention clip{down, reversed};
                sceneSettings.set_camera({math::identity(),
                                          perspective ? math::perspective(1, 1, 0.1f, 100, clip)
                                                      : math::orthographic(-5, 5, -5, 5, 0.1f, 100, clip),
                                          clip});
                sceneSettings.model =
                    math::translation({1.5f, -2, -4}) * math::rotation({0, 1, 0}, 0.7f) * math::scale({1.3f, 0.5f, 2});
                tests::FakeCommands transformed;
                renderer.recordFrame(transformed, sceneSettings);
                const auto matrix = uploaded<math::Mat4>(device, 928);
                require(math::is_finite(matrix), "Directional fit matrix is nonfinite");
                for (const float x : {-2.4f, 2.4f})
                    for (const float y : {-0.5f, 0.5f})
                        for (const float z : {-3.0f, -2.0f}) {
                            const auto world = math::transform_point(sceneSettings.model, {x, y, z});
                            const auto ndc = math::transform_point(matrix, world);
                            require(std::abs(ndc.x) <= 1.001f && std::abs(ndc.y) <= 1.001f && ndc.z >= -0.001f &&
                                        ndc.z <= 1.001f,
                                    "Directional shadow fit clips a transformed bounds corner");
                        }
                const auto& pass = named(transformed, "shadow");
                require(pass.clears[0].depth == 1 && pass.pipeline.depthCompare == rhi::DepthCompare::Less,
                        "Scene reversed Z leaked into shadow depth");
                renderer.frameSubmitted();
            }
    sceneSettings.lights[0].type = render::LightType::Point;
    tests::FakeCommands point;
    renderer.recordFrame(point, sceneSettings);
    require(std::ranges::find(point.passes, "shadow") == point.passes.end() && uploaded<math::Vec4>(device, 992).x == 0,
            "Point selection incorrectly emitted a single-face shadow map");
    renderer.frameSubmitted();
    sceneSettings.lights[0].type = render::LightType::Spot;
    sceneSettings.lights[0].position = {0, 0, 0};
    sceneSettings.lights[0].direction = {0, 0, -1};
    sceneSettings.lights[0].range = 30;
    tests::FakeCommands spot;
    renderer.recordFrame(spot, sceneSettings);
    require(spot.passes.front() == "shadow" && uploaded<math::Vec4>(device, 992).x == 1 &&
                math::is_finite(uploaded<math::Mat4>(device, 928)),
            "Valid spot selection did not render a shadow map");
}
void capabilitiesAndRollback(const std::filesystem::path& capture) {
    for (unsigned selected = 0; selected < 3; ++selected) {
        tests::FakeDevice device;
        for (unsigned i = 0; i < selected; ++i) device.depthSampledSupport[i] = false;
        render::Renderer renderer(device);
        require(renderer.supportsShadows(), "Depth format fallback lost shadow support");
        auto shadow = renderer.shadows();
        shadow.enabled = true;
        shadow.resolution = 16;
        renderer.setShadows(shadow);
        renderer.uploadModel(fixture());
        tests::FakeCommands commands;
        renderer.recordFrame(commands, settings());
        constexpr std::array formats{rhi::Format::D32Float, rhi::Format::D16Unorm, rhi::Format::D24UnormS8};
        require(named(commands, "shadow").attachmentDescriptions[0].format == formats[selected],
                "Depth format preference/fallback changed");
    }
    {
        tests::FakeDevice device;
        device.depthSampledSupport.fill(false);
        render::Renderer renderer(device);
        require(!renderer.supportsShadows() && renderer.maximumShadowResolution() == 0,
                "Unsupported sampled depth advertised shadows");
        auto shadow = renderer.shadows();
        shadow.enabled = true;
        rejects([&] { renderer.setShadows(shadow); }, "Unsupported shadows were enabled");
        require(!renderer.shadows().enabled, "Unsupported shadow request changed settings");
        tests::FakeCommands commands;
        renderer.recordFrame(commands, settings());
        require(binding(bound(named(commands, "scene"), 2), 0).texture, "Unsupported depth lacks fallback descriptor");
    }
    {
        tests::FakeDevice device;
        device.depthSampledMaximum[0] = {128, 128};
        render::Renderer renderer(device);
        require(renderer.maximumShadowResolution() == 128,
                "Small sampled-depth device reported the wrong shadow maximum");
        const auto initial = renderer.shadows();
        require(initial.resolution <= renderer.maximumShadowResolution(),
                "Initial shadow resolution exceeds the sampled-depth device limit");
        renderer.setShadows(initial);
        require(renderer.shadows() == initial, "Reapplying initial shadow settings changed them");
    }
    {
        tests::FakeDevice device;
        device.depthSampledMaximum[0] = {300, 700};
        device.caps.maximumFramebufferDimension = 600;
        render::Renderer renderer(device);
        require(renderer.maximumShadowResolution() == 256,
                "Shadow maximum ignored format extent or power-of-two limit");
        auto shadow = renderer.shadows();
        shadow.enabled = true;
        shadow.resolution = 512;
        rejects([&] { renderer.setShadows(shadow); }, "Shadow resolution above format limit was accepted");
    }
    tests::FakeDevice device;
    render::Renderer renderer(device);
    renderer.resizeViewport(19, 17);
    renderer.uploadModel(fixture());
    renderer.setAntialiasing({1, true, true});
    auto shadow = renderer.shadows();
    shadow.enabled = true;
    shadow.resolution = 64;
    renderer.setShadows(shadow);
    const auto sceneSettings = settings();
    const auto submit = [&] {
        tests::FakeCommands commands;
        renderer.recordFrame(commands, sceneSettings);
        renderer.frameSubmitted();
        return commands;
    };
    submit();
    const auto before = submit();
    const auto output = renderer.outputTexture();
    const auto count = renderer.temporalFrameCount();
    const auto map = named(before, "shadow").framebuffer.attachments[0];
    const auto retained = [&] {
        require(renderer.shadows() == shadow &&
                    renderer.antialiasing() == render::AntialiasingSettings{1, true, true} &&
                    renderer.outputTexture() == output && renderer.temporalFrameCount() == count,
                "Failed shadow replacement changed settings, AA, output or history");
        renderer.captureViewport(capture);
        require(device.lastReadback == output.get(), "Failed shadow replacement changed captured output");
        // FXAA is the fixed final output: speculative TAA recording can select
        // its pending history without replacing this output or advancing count.
        tests::FakeCommands commands;
        renderer.recordFrame(commands, sceneSettings);
        require(named(commands, "shadow").framebuffer.attachments[0] == map &&
                    named(commands, "scene").draws.size() == 3,
                "Failed shadow replacement lost resources or model");
    };
    auto replacement = shadow;
    replacement.resolution = 128;
    device.failTexture = true;
    rejects([&] { renderer.setShadows(replacement); }, "Injected shadow allocation failure was accepted");
    retained();
    device.failPipeline = true;
    rejects([&] { renderer.setShadows(replacement); }, "Injected shadow pipeline failure was accepted");
    retained();
    device.failBindingSet = true;
    rejects([&] { renderer.setShadows(replacement); }, "Injected shadow descriptor failure was accepted");
    retained();
    device.failWaitIdle = true;
    rejects([&] { renderer.setShadows(replacement); }, "Injected shadow wait failure was accepted");
    retained();
    for (const auto resolution : {0u, 8u, 17u, 8192u}) {
        auto invalid = shadow;
        invalid.resolution = resolution;
        rejects([&] { renderer.setShadows(invalid); }, "Invalid shadow resolution accepted");
        retained();
    }
    for (const auto bad : {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        auto invalid = shadow;
        invalid.depthBias = bad;
        rejects([&] { renderer.setShadows(invalid); }, "Invalid shadow bias accepted");
        retained();
        invalid = shadow;
        invalid.slopeBias = bad;
        rejects([&] { renderer.setShadows(invalid); }, "Invalid slope bias accepted");
        retained();
    }
    auto invalid = shadow;
    invalid.lightIndex = render::kMaxLights;
    rejects([&] { renderer.setShadows(invalid); }, "Invalid shadow light index accepted");
    retained();
    renderer.setShadows(shadow);
    require(renderer.temporalFrameCount() == count, "Identical shadow settings reset history");
    replacement.depthBias += 0.0001f;
    renderer.setShadows(replacement);
    require(renderer.temporalFrameCount() == 0, "Changed shadow settings retained stale TAA");
}
}
int main(int argc, char** argv) {
    try {
        resourcesAndPolicy();
        capabilitiesAndRollback(argc > 1 ? std::filesystem::path(argv[1])
                                         : std::filesystem::temp_directory_path() / "aeg-shadow-contract.ppm");
        std::cout << "PASS shadow commands, sampled depth, material policy, bounds, fallback and rollback contracts\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
