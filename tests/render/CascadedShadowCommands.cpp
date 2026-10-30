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
template <class T> T bytesAt(const std::vector<std::byte>& bytes, std::size_t offset) {
    require(offset + sizeof(T) <= bytes.size(), "Cascade uniform/push read outside ABI");
    T result;
    std::memcpy(&result, bytes.data() + offset, sizeof(T));
    return result;
}
template <class T> T uploaded(const tests::FakeDevice& device, std::size_t offset) {
    require(device.uniform, "Missing scene uniform");
    return bytesAt<T>(device.uniform->bytes, offset);
}
const tests::PassSnapshot& scene(const tests::FakeCommands& commands) {
    const auto at = std::ranges::find(commands.passes, "scene");
    require(at != commands.passes.end(), "Missing scene pass");
    return commands.recordedPasses.at(std::size_t(at - commands.passes.begin()));
}
const tests::BoundSetSnapshot& shadowSet(const tests::PassSnapshot& pass) {
    const auto at = std::ranges::find_if(pass.sets, [](const auto& set) { return set.index == 2; });
    require(at != pass.sets.end(), "Missing cascade scene descriptor set");
    return *at;
}
const rhi::BindingWrite& binding(const tests::BoundSetSnapshot& set, unsigned index) {
    const auto at = std::ranges::find_if(set.writes, [=](const auto& write) { return write.binding == index; });
    require(at != set.writes.end(), "Missing separate cascade texture binding " + std::to_string(index));
    return *at;
}
bool white(const rhi::Texture* texture) {
    if (!texture || texture->description().format != rhi::Format::Rgba8Unorm ||
        texture->description().extent != rhi::Extent2D{1, 1})
        return false;
    return dynamic_cast<const tests::FakeTexture&>(*texture).uploaded == std::vector<std::byte>(4, std::byte{255});
}

// Snapshot mapped bytes at every real renderer draw. Replacing cascade push
// indices with per-pass writes into one shared UBO must fail this test.
class RecordingCommands final : public rhi::CommandList {
public:
    explicit RecordingCommands(tests::FakeDevice& device) : device_(device) {}
    tests::FakeCommands commands;
    std::vector<rhi::Framebuffer*> shadowFramebuffers;
    std::vector<std::vector<std::byte>> shadowUniforms;
    void beginRenderPass(rhi::RenderPass& pass, rhi::Framebuffer& framebuffer, rhi::Extent2D extent,
                         std::span<const rhi::ClearValue> clears) override {
        shadow_ = dynamic_cast<tests::FakePass&>(pass).desc.colors.empty();
        if (shadow_) shadowFramebuffers.push_back(&framebuffer);
        commands.beginRenderPass(pass, framebuffer, extent, clears);
    }
    void endRenderPass() override { commands.endRenderPass(); }
    void setViewport(rhi::Extent2D value) override { commands.setViewport(value); }
    void bindPipeline(rhi::GraphicsPipeline& value) override { commands.bindPipeline(value); }
    void bindComputePipeline(rhi::ComputePipeline& value) override { commands.bindComputePipeline(value); }
    void bindSet(rhi::PipelineLayout& layout, std::uint32_t index, rhi::BindingSet& set,
                 rhi::PipelineBindPoint point = rhi::PipelineBindPoint::Graphics) override {
        commands.bindSet(layout, index, set, point);
    }
    void bindVertexBuffer(rhi::Buffer& value) override { commands.bindVertexBuffer(value); }
    void bindIndexBuffer(rhi::Buffer& value) override { commands.bindIndexBuffer(value); }
    void pushConstants(rhi::PipelineLayout& layout, std::span<const std::byte> value) override {
        commands.pushConstants(layout, value);
    }
    void draw(std::uint32_t count) override { commands.draw(count); }
    void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override { commands.dispatch(x, y, z); }
    void drawIndexed(std::uint32_t count) override {
        if (shadow_) shadowUniforms.push_back(device_.uniform->bytes);
        commands.drawIndexed(count);
    }
    void transition(rhi::Texture& texture, rhi::ResourceState before, rhi::ResourceState after) override {
        commands.transition(texture, before, after);
    }
    void copyBuffer(rhi::Buffer& from, rhi::Buffer& to, std::size_t count) override {
        commands.copyBuffer(from, to, count);
    }
    void copyTextureToBuffer(rhi::Texture& from, rhi::Buffer& to, rhi::TextureSubresource subresource = {}) override {
        commands.copyTextureToBuffer(from, to, subresource);
    }

private:
    tests::FakeDevice& device_;
    bool shadow_{};
};
assets::ModelData fixture() {
    assets::ModelData model;
    model.name = "Cascade command policy";
    for (unsigned i = 0; i < 3; ++i) {
        auto& material = model.materials.emplace_back();
        material.alphaMode = i == 0   ? assets::AlphaMode::Opaque
                             : i == 1 ? assets::AlphaMode::Mask
                                      : assets::AlphaMode::Blend;
        material.baseColor = {0.4f, 0.5f, 0.6f, 0.7f};
        material.alphaCutoff = 0.37f;
        auto& mesh = model.meshes.emplace_back();
        const float x = float(i) - 1;
        mesh.vertices = {{{x - 0.3f, -0.4f, -3}, {0, 0, 1}, {0, 0}},
                         {{x + 0.3f, -0.4f, -3}, {0, 0, 1}, {1, 0}},
                         {{x, 0.4f, -2}, {0, 0, 1}, {0.5f, 1}}};
        mesh.indices = {0, 1, 2};
        mesh.materialIndex = i;
    }
    return model;
}
render::RenderSettings settings() {
    render::RenderSettings result;
    result.lights[0].direction = {0.4f, -0.2f, -1};
    result.set_camera({math::identity(), math::perspective(1, 1, 0.1f, 20, {true, false}), {true, false}});
    return result;
}
void checkRecorded(tests::FakeDevice& device, RecordingCommands& recorded, unsigned count, unsigned resolution,
                   bool postprocess) {
    const auto& commands = recorded.commands;
    std::vector<std::string> expected(count, "shadow");
    expected.push_back("scene");
    if (postprocess) {
        expected.push_back("taa");
        expected.push_back("tone");
        expected.push_back("fxaa");
    } else {
        expected.push_back("tone");
    }
    require(commands.passes == expected, "Cascade passes must precede scene, TAA, tone mapping and FXAA");
    require(device.uniform->bytes.size() == 1264, "Cascade scene uniform ABI must be 1264 bytes");
    require(uploaded<math::Vec4>(device, 1184) == math::Vec4(1, 0, 1.0f / resolution, float(count)),
            "Cascade enable, light index, inverse resolution or active-count ABI is wrong");
    const auto& scenePass = scene(commands);
    require(scenePass.draws.size() == 3, "Cascade feature changed opaque/masked/blended receiving policy");
    const auto& descriptors = shadowSet(scenePass);
    require(descriptors.writes.size() == 4, "Scene must bind four distinct sampled texture bindings");
    for (unsigned i = 0; i < 4; ++i) {
        const auto& write = binding(descriptors, i);
        require(write.texture && write.sampler && !write.buffer, "Cascade descriptor is incomplete");
        require(dynamic_cast<tests::FakeSampler&>(*write.sampler).desc.filter == rhi::Filter::Nearest,
                "PCF cascade sampler must use nearest texels");
        if (i >= count) {
            require(white(write.texture), "Inactive cascade descriptor must use initialized white texture");
            continue;
        }
        const auto& pass = commands.recordedPasses[i];
        require(pass.description.colors.empty() && pass.description.depth == 0u &&
                    pass.description.attachments.size() == 1 && pass.framebuffer.attachments.size() == 1 &&
                    pass.framebuffer.extent == rhi::Extent2D{resolution, resolution},
                "Cascade is not a separate depth-only framebuffer");
        const auto& desc = pass.attachmentDescriptions[0];
        require(rhi::isDepthFormat(desc.format) && desc.samples == 1 &&
                    rhi::contains(desc.usage, rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled),
                "Cascade map is not single-sample sampled depth");
        const auto& attachment = pass.description.attachments[0];
        require(attachment.load == rhi::LoadOp::Clear && attachment.store == rhi::StoreOp::Store &&
                    attachment.finalState == rhi::ResourceState::DepthAttachment && pass.clears.size() == 1 &&
                    pass.clears[0].depth == 1 && pass.pipeline.depthCompare == rhi::DepthCompare::Less &&
                    pass.pipeline.depthTest && pass.pipeline.depthWrite && pass.pipeline.colorAttachmentCount == 0,
                "Cascade clear/store/canonical depth policy changed");
        require(write.texture == pass.framebuffer.attachments[0], "Scene samples a different cascade map");
        for (unsigned j = 0; j < i; ++j)
            require(write.texture != binding(descriptors, j).texture &&
                        recorded.shadowFramebuffers[i] != recorded.shadowFramebuffers[j],
                    "Cascades alias a texture or framebuffer");
        require(pass.draws.size() == 2, "Each cascade must render opaque and masked casters only");
        for (const auto& draw : pass.draws) {
            require(draw.constants.size() == 64 && bytesAt<std::uint32_t>(draw.constants, 48) == i,
                    "Each caster must select its cascade through dedicated 64-byte shadow push constants");
            const auto counterpart =
                std::ranges::find_if(scenePass.draws, [&](const auto& value) { return draw.indices == value.indices; });
            require(
                counterpart != scenePass.draws.end() && counterpart->constants.size() == 48 &&
                    std::equal(counterpart->constants.begin(), counterpart->constants.end(), draw.constants.begin()),
                "Shadow push prefix no longer preserves scene material alpha/cutoff bytes");
        }
        const auto matrix = uploaded<math::Mat4>(device, 928 + i * sizeof(math::Mat4));
        require(math::is_finite(matrix) && matrix != math::identity(), "Active cascade matrix is invalid");
        if (i) require(matrix != uploaded<math::Mat4>(device, 928), "Distinct cascade intervals reuse one matrix");
    }
    require(recorded.shadowUniforms.size() == count * 2, "Missing per-draw uniform snapshots");
    for (const auto& bytes : recorded.shadowUniforms)
        require(bytes == device.uniform->bytes, "Cascade recording rewrote a shared uniform between shadow draws");
}
void passesAndActiveDescriptors() {
    tests::FakeDevice device;
    render::Renderer renderer(device);
    renderer.resizeViewport(31, 27);
    renderer.uploadModel(fixture());
    renderer.setAntialiasing({1, true, true});
    auto sceneSettings = settings();
    auto shadow = renderer.shadows();
    shadow.enabled = true;
    shadow.resolution = 64;
    // A first Spot frame with default capacity four must not bind undefined maps.
    renderer.setShadows(shadow);
    sceneSettings.lights[0].type = render::LightType::Spot;
    sceneSettings.lights[0].position = {0, 0, 0};
    sceneSettings.lights[0].direction = {0, 0, -1};
    sceneSettings.lights[0].range = 20;
    RecordingCommands firstSpot(device);
    renderer.recordFrame(firstSpot, sceneSettings);
    checkRecorded(device, firstSpot, 1, 64, true);
    const auto spotSet = shadowSet(scene(firstSpot.commands)).set;
    renderer.frameSubmitted();
    sceneSettings.lights[0].type = render::LightType::Directional;
    sceneSettings.lights[0].direction = {0.4f, -0.2f, -1};
    RecordingCommands four(device);
    renderer.recordFrame(four, sceneSettings);
    checkRecorded(device, four, 4, 64, true);
    require(shadowSet(scene(four.commands)).set != spotSet, "Spot and four-cascade frames reused mutable descriptors");
    renderer.frameSubmitted();
    sceneSettings.lights[0].type = render::LightType::Spot;
    sceneSettings.lights[0].direction = {0, 0, -1};
    RecordingCommands spotAgain(device);
    renderer.recordFrame(spotAgain, sceneSettings);
    checkRecorded(device, spotAgain, 1, 64, true);
    require(shadowSet(scene(spotAgain.commands)).set == spotSet, "Spot descriptor variant was needlessly replaced");
    renderer.frameSubmitted();
    sceneSettings.lights[0].type = render::LightType::Directional;
    for (unsigned count = 1; count <= 4; ++count) {
        shadow.cascadeCount = count;
        shadow.pcfRadius = count % 3;
        renderer.setShadows(shadow);
        RecordingCommands frame(device);
        renderer.recordFrame(frame, sceneSettings);
        checkRecorded(device, frame, count, 64, true);
        const auto bias = uploaded<math::Vec4>(device, 1200);
        require(bias == math::Vec4(shadow.depthBias, shadow.slopeBias, float(shadow.pcfRadius), 0),
                "PCF radius and bias ABI changed");
        renderer.frameSubmitted();
    }
}
void invalidFitDisablesWholeFrame() {
    tests::FakeDevice device;
    render::Renderer renderer(device);
    renderer.resizeViewport(23, 21);
    renderer.uploadModel(fixture());
    renderer.setAntialiasing({1, true, true});
    auto shadow = renderer.shadows();
    shadow.enabled = true;
    shadow.resolution = 64;
    renderer.setShadows(shadow);
    auto sceneSettings = settings();
    for (unsigned i = 0; i < 2; ++i) {
        RecordingCommands valid(device);
        renderer.recordFrame(valid, sceneSettings);
        checkRecorded(device, valid, 4, 64, true);
        renderer.frameSubmitted();
    }
    const auto camera = sceneSettings.viewProjection;
    const auto output = renderer.outputTexture();
    const auto history = renderer.temporalFrameCount();
    for (unsigned failure = 0; failure < 2; ++failure) {
        sceneSettings.viewProjection = failure == 0 ? math::Mat4{} : camera;
        if (failure) sceneSettings.viewProjection.at(0, 0) = std::numeric_limits<float>::quiet_NaN();
        RecordingCommands invalid(device);
        renderer.recordFrame(invalid, sceneSettings);
        require(invalid.commands.passes == std::vector<std::string>{"scene", "taa", "tone", "fxaa"},
                "Invalid camera fit emitted partial or stale cascade passes");
        const auto info = uploaded<math::Vec4>(device, 1184);
        require(info.x == 0 && info.w == 0 && math::is_finite(info),
                "Invalid camera fit retained enabled cascade metadata");
        for (unsigned i = 0; i < 4; ++i) {
            require(uploaded<math::Mat4>(device, 928 + i * sizeof(math::Mat4)) == math::identity(),
                    "Invalid camera fit published nonidentity or stale cascade matrix");
            require(white(binding(shadowSet(scene(invalid.commands)), i).texture),
                    "Invalid camera fit retained a stale real-map descriptor");
        }
        require(renderer.shadows() == shadow && renderer.outputTexture() == output &&
                    renderer.temporalFrameCount() == history,
                "Unsubmitted invalid-fit recording changed settings, output or submitted temporal count");
    }
    sceneSettings.viewProjection = camera;
    RecordingCommands restored(device);
    renderer.recordFrame(restored, sceneSettings);
    checkRecorded(device, restored, 4, 64, true);
    renderer.frameSubmitted();
    require(renderer.temporalFrameCount() == history + 1, "Restored valid camera did not recover temporal submission");
}
void rollbackAndValidation(const std::filesystem::path& capture) {
    tests::FakeDevice device;
    render::Renderer renderer(device);
    renderer.resizeViewport(19, 17);
    renderer.uploadModel(fixture());
    renderer.setAntialiasing({1, true, true});
    auto shadow = renderer.shadows();
    shadow.enabled = true;
    shadow.resolution = 64;
    renderer.setShadows(shadow);
    auto sceneSettings = settings();
    const auto submit = [&] {
        RecordingCommands frame(device);
        renderer.recordFrame(frame, sceneSettings);
        renderer.frameSubmitted();
        return frame;
    };
    submit();
    const auto before = submit();
    const auto output = renderer.outputTexture();
    const auto history = renderer.temporalFrameCount();
    const auto descriptors = shadowSet(scene(before.commands));
    const auto retained = [&] {
        require(renderer.shadows() == shadow && renderer.outputTexture() == output &&
                    renderer.antialiasing() == render::AntialiasingSettings{1, true, true} &&
                    renderer.temporalFrameCount() == history,
                "Failed cascade transaction changed settings, output, AA or temporal history");
        renderer.captureViewport(capture);
        require(device.lastReadback == output.get(), "Failed cascade transaction changed captured output");
        RecordingCommands frame(device);
        renderer.recordFrame(frame, sceneSettings);
        checkRecorded(device, frame, 4, 64, true);
        const auto& actual = shadowSet(scene(frame.commands));
        require(actual.set == descriptors.set, "Failed cascade transaction replaced live descriptor set");
        for (unsigned i = 0; i < 4; ++i)
            require(binding(actual, i).texture == binding(descriptors, i).texture,
                    "Partial allocation failure discarded an old cascade map");
    };
    auto replacement = shadow;
    replacement.resolution = 128;
    for (unsigned nth : {2u, 3u}) {
        device.failNthTexture = nth;
        rejects([&] { renderer.setShadows(replacement); }, "Partial cascade texture allocation did not fail");
        require(device.failNthTexture == 0, "Partial allocation failure hook was not reached");
        retained();
    }
    for (unsigned failure = 0; failure < 3; ++failure) {
        device.failPipeline = failure == 0;
        device.failBindingSet = failure == 1;
        device.failWaitIdle = failure == 2;
        rejects([&] { renderer.setShadows(replacement); }, "Cascade transaction failure was accepted");
        retained();
    }
    const auto invalid = [&](auto change) {
        auto candidate = shadow;
        change(candidate);
        rejects([&] { renderer.setShadows(candidate); }, "Invalid CSM settings were accepted");
        retained();
    };
    for (unsigned value : {0u, 5u, std::numeric_limits<unsigned>::max()})
        invalid([=](auto& s) { s.cascadeCount = value; });
    for (unsigned value : {3u, std::numeric_limits<unsigned>::max()}) invalid([=](auto& s) { s.pcfRadius = value; });
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (float value : {-0.01f, 1.01f, nan, inf}) invalid([=](auto& s) { s.splitLambda = value; });
    for (float value : {-0.01f, nan, inf}) invalid([=](auto& s) { s.maxDistance = value; });
    for (float value : {-0.01f, 0.301f, nan, inf}) invalid([=](auto& s) { s.blendFraction = value; });
    for (float value : {-0.01f, 0.501f, nan, inf}) invalid([=](auto& s) { s.distanceFadeFraction = value; });
    renderer.setShadows(shadow);
    require(renderer.temporalFrameCount() == history, "No-op CSM settings reset TAA history");
    replacement = shadow;
    replacement.splitLambda = 1;
    replacement.maxDistance = 15;
    replacement.blendFraction = 0.3f;
    replacement.distanceFadeFraction = 0.5f;
    replacement.pcfRadius = 2;
    renderer.setShadows(replacement);
    require(renderer.temporalFrameCount() == 0, "Changed cascade settings retained stale temporal history");
    submit();
    sceneSettings.model = math::translation({0.01f, 0, 0});
    submit();
    require(renderer.temporalFrameCount() == 2, "Ordinary shadowed model motion reset TAA history");
    sceneSettings.set_camera(
        {math::translation({-0.01f, 0, 0}), math::perspective(1, 1, 0.1f, 20, {true, false}), {true, false}});
    submit();
    require(renderer.temporalFrameCount() == 3, "Ordinary shadowed camera motion reset TAA history");
    const auto resetsHistory = [&](auto change) {
        auto candidate = renderer.shadows();
        change(candidate);
        renderer.setShadows(candidate);
        require(renderer.temporalFrameCount() == 0, "One changed CSM field failed to reset temporal history");
        submit();
        renderer.setShadows(candidate);
        require(renderer.temporalFrameCount() == 1, "Reapplying one CSM field reset temporal history");
    };
    resetsHistory([](auto& s) { s.cascadeCount = 3; });
    resetsHistory([](auto& s) { s.splitLambda = 0; });
    resetsHistory([](auto& s) { s.maxDistance = 0; });
    resetsHistory([](auto& s) { s.blendFraction = 0; });
    resetsHistory([](auto& s) { s.distanceFadeFraction = 0; });
    resetsHistory([](auto& s) { s.pcfRadius = 0; });
}
}
int main(int argc, char** argv) {
    try {
        passesAndActiveDescriptors();
        invalidFitDisablesWholeFrame();
        rollbackAndValidation(argc > 1 ? std::filesystem::path(argv[1]) : "cascaded-shadow-commands.ppm");
        std::cout << "Cascaded shadow commands: separate passes/maps, immutable active descriptors, per-draw push, "
                     "partial rollback, validation and temporal history passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Cascaded shadow commands failed: " << error.what() << '\n';
        return 1;
    }
}
