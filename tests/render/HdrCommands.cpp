#include <Renderer.hpp>
#include "../rhi/fake/FakeDevice.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace aeg;
constexpr auto usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySource;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template <class F> void rejects(F&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}
const tests::PassSnapshot& named(const tests::FakeCommands& commands, const std::string& name) {
    const auto found = std::ranges::find(commands.passes, name);
    require(found != commands.passes.end(), "Missing required renderer pass");
    return commands.recordedPasses.at(static_cast<std::size_t>(found - commands.passes.begin()));
}
rhi::Texture* source(const tests::PassSnapshot& pass) {
    require(pass.sets.size() == 1 && pass.sets[0].writes.size() == 1, "Postprocess requires one immutable source");
    return pass.sets[0].writes[0].texture;
}
struct TonePush {
    std::uint32_t mapper;
    float ev;
    std::array<float, 2> padding;
};
TonePush tonePush(const tests::FakeCommands& commands) {
    const auto& tone = named(commands, "tone");
    require(tone.draws.size() == 1 && tone.draws[0].constants.size() == 16, "Tone pass must snapshot its 16-byte push");
    const auto& layout = dynamic_cast<tests::FakePipelineLayout&>(*tone.pipeline.layout).desc;
    require(layout.pushConstantBytes == 16 && layout.pushConstantStages == rhi::ShaderStage::Fragment,
            "Tone push visibility/layout is incorrect");
    TonePush push;
    std::memcpy(&push, tone.draws[0].constants.data(), sizeof(push));
    return push;
}
void floatingSceneAndDisplayOrder() {
    tests::FakeDevice device;
    render::Renderer renderer(device);
    require(renderer.toneMapping() == render::ToneMappingSettings{}, "Renderer defaults must select zero-EV ACES");
    require(renderer.outputTexture()->description().format == rhi::Format::Rgba8Unorm,
            "Unsubmitted GUI fallback must never expose a raw HDR texture");
    tests::FakeCommands commands;
    renderer.recordFrame(commands, {});
    const auto& scene = commands.recordedPasses.front();
    require(scene.attachmentDescriptions.front().format == rhi::Format::Rgba16Float,
            "Scene must retain radiance above one in a floating attachment");
    require(commands.passes == std::vector<std::string>{"scene", "tone"},
            "Tone mapping must be mandatory after HDR scene");
    const auto& tone = named(commands, "tone");
    require(source(tone) == scene.framebuffer.attachments[0] &&
                tone.attachmentDescriptions[0].format == rhi::Format::Rgba8Unorm &&
                tone.framebuffer.attachments[0] != scene.framebuffer.attachments[0] &&
                renderer.outputTexture().get() == tone.framebuffer.attachments[0],
            "Tone mapping must read HDR and publish a distinct display-linear target");
    const auto push = tonePush(commands);
    require(push.mapper == 1 && push.ev == 0 && push.padding == std::array<float, 2>{},
            "Default ACES push is incorrect");
    renderer.setAntialiasing({4, true, false});
    tests::FakeCommands msaa;
    renderer.recordFrame(msaa, {});
    require(msaa.passes == std::vector<std::string>{"scene", "tone", "fxaa"}, "MSAA must resolve before tone and FXAA");
    require(named(msaa, "scene").attachmentDescriptions[0].samples == 4 &&
                named(msaa, "scene").attachmentDescriptions[0].format == rhi::Format::Rgba16Float &&
                source(named(msaa, "tone"))->description().samples == 1 &&
                source(named(msaa, "fxaa")) == named(msaa, "tone").framebuffer.attachments[0],
            "MSAA HDR resolve or fixed tone-to-FXAA source is incorrect");
}
void exposurePreservesHdrHistory() {
    tests::FakeDevice device;
    render::Renderer renderer(device);
    renderer.setAntialiasing({1, true, true});
    for (int i = 0; i < 2; ++i) {
        tests::FakeCommands commands;
        renderer.recordFrame(commands, {});
        require(commands.passes == std::vector<std::string>{"scene", "taa", "tone", "fxaa"},
                "TAA must accumulate unexposed HDR before tone mapping");
        require(source(named(commands, "tone")) == named(commands, "taa").framebuffer.attachments[0],
                "Tone descriptor must sample the current TAA write target");
        renderer.frameSubmitted();
    }
    require(renderer.temporalFrameCount() == 2, "History fixture did not submit two frames");
    const auto output = renderer.outputTexture();
    device.failTexture = true;
    device.failBindingSet = true;
    renderer.setToneMapping({render::ToneMapper::Linear, -2});
    require(renderer.temporalFrameCount() == 2 && renderer.outputTexture() == output,
            "Exposure/mapper changes must preserve unexposed history and last output without allocating");
    device.failTexture = false;
    device.failBindingSet = false;
    tests::FakeCommands abandoned, retry;
    renderer.recordFrame(abandoned, {});
    renderer.recordFrame(retry, {});
    require(source(named(abandoned, "tone")) == source(named(retry, "tone")),
            "Unsubmitted recording must not advance immutable tone history descriptors");
    require(tonePush(retry).mapper == 0 && tonePush(retry).ev == -2, "New exposure was not recorded in tone pass");
    renderer.frameSubmitted();
    require(renderer.temporalFrameCount() == 3,
            "Postprocess-only setting changes reset or advanced history incorrectly");
    const auto original = renderer.toneMapping();
    for (float invalid :
         {-13.0f, 13.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        rejects([&] { renderer.setToneMapping({render::ToneMapper::Aces, invalid}); }, "Invalid EV accepted");
    rejects([&] { renderer.setToneMapping({static_cast<render::ToneMapper>(99), 0}); }, "Invalid mapper accepted");
    require(renderer.toneMapping() == original && renderer.temporalFrameCount() == 3,
            "Rejected tone settings mutated state");
}
void formatCapabilitiesAndSampleLimits() {
    tests::FakeDevice fallback;
    fallback.textureSupportOverrides.push_back(
        {rhi::Format::Rgba16Float, rhi::TextureUsage::ColorAttachment, {true, 15, {4096, 4096}, true, false}});
    render::Renderer renderer(fallback);
    tests::FakeCommands commands;
    renderer.recordFrame(commands, {});
    require(commands.recordedPasses[0].attachmentDescriptions[0].format == rhi::Format::Rgba32Float,
            "Missing half-float alpha blending must select full-float HDR");
    tests::FakeDevice unsupported;
    for (auto format : {rhi::Format::Rgba16Float, rhi::Format::Rgba32Float})
        unsupported.textureSupportOverrides.push_back({format, rhi::TextureUsage::ColorAttachment, {}});
    rejects([&] { render::Renderer invalid(unsupported); }, "Renderer silently fell back to clipped UNORM scene");
    tests::FakeDevice limited;
    limited.textureSupportOverrides.push_back(
        {rhi::Format::Rgba16Float, rhi::TextureUsage::ColorAttachment, {true, 5, {512, 256}, true, true}});
    limited.textureSupportOverrides.push_back({rhi::Format::Rgba8Unorm, usage, {true, 1, {128, 64}, true, true}});
    render::Renderer constrained(limited);
    // The existing viewport policy uses the smaller per-axis limit for both dimensions.
    require(constrained.supportedMsaaSamples() == 5 &&
                constrained.outputTexture()->description().extent == rhi::Extent2D{64, 64},
            "HDR MSAA or display-target extent limits were ignored");
    constrained.setAntialiasing({8, true, false});
    require(constrained.antialiasing().msaaSamples == 4,
            "MSAA fallback must use HDR scene support, not single-sample tone support");
}
void finiteClearAndReplacementRollback() {
    tests::FakeDevice device;
    render::Renderer renderer(device);
    render::RenderSettings settings;
    settings.background = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity(), 1};
    tests::FakeCommands bad;
    renderer.recordFrame(bad, settings);
    require(bad.recordedPasses[0].clears[0].color == std::array<float, 4>{0, 65504, 0, 1},
            "HDR clear must sanitize nonfinite values before half storage");
    settings.background = {-.5f, 4, 1e30f, 1};
    tests::FakeCommands finite;
    renderer.recordFrame(finite, settings);
    require(finite.recordedPasses[0].clears[0].color == std::array<float, 4>{0, 4, 65504, 1},
            "HDR clear must preserve values above one and enforce finite storage ceiling");
    renderer.setAntialiasing({1, true, true});
    tests::FakeCommands submitted;
    renderer.recordFrame(submitted, {});
    renderer.frameSubmitted();
    const auto output = renderer.outputTexture();
    const auto aa = renderer.antialiasing();
    device.failNthTexture = 3;
    rejects([&] { renderer.resizeViewport(321, 123); }, "Partial HDR viewport replacement did not fail");
    require(renderer.outputTexture() == output && renderer.temporalFrameCount() == 1 && renderer.antialiasing() == aa,
            "Partial target failure discarded output, history or AA settings");
    device.failNthTexture = 4;
    rejects([&] { renderer.setAntialiasing({4, true, false}); }, "Partial MSAA/tone replacement did not fail");
    require(renderer.outputTexture() == output && renderer.temporalFrameCount() == 1 && renderer.antialiasing() == aa,
            "Tone/MSAA allocation failure committed partial frame state");
}
}
int main() {
    int failures = 0;
    const std::pair<const char*, std::function<void()>> tests[]{
        {"floating scene and display order", floatingSceneAndDisplayOrder},
        {"exposure preserves HDR history", exposurePreservesHdrHistory},
        {"format capabilities", formatCapabilitiesAndSampleLimits},
        {"HDR clears and rollback", finiteClearAndReplacementRollback}};
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
