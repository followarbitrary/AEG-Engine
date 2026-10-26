#include <Renderer.hpp>
#include "../rhi/fake/FakeDevice.hpp"
#include <iostream>

namespace {
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
using namespace aeg;
const tests::PassSnapshot& pass(const tests::FakeCommands& commands, std::size_t index) {
    require(commands.recordedPasses.size() == commands.passes.size(), "Render pass resources were not recorded");
    const auto& result = commands.recordedPasses.at(index);
    require(result.pass && result.framebuffer.pass == result.pass, "Framebuffer belongs to the wrong render pass");
    require(result.framebuffer.attachments.size() == result.description.attachments.size() &&
                result.attachmentDescriptions.size() == result.description.attachments.size(),
            "Framebuffer attachment count disagrees with render pass");
    for (std::size_t i = 0; i < result.description.attachments.size(); ++i) {
        const auto& texture = result.attachmentDescriptions[i];
        const auto& attachment = result.description.attachments[i];
        require(result.framebuffer.attachments[i] && texture.extent == result.framebuffer.extent &&
                    texture.format == attachment.format && texture.samples == attachment.samples,
                "Framebuffer texture does not match its attachment descriptor");
    }
    return result;
}
const tests::BoundSetSnapshot& set(const tests::PassSnapshot& recorded, std::uint32_t index) {
    const tests::BoundSetSnapshot* result{};
    for (const auto& bound : recorded.sets)
        if (bound.index == index) {
            require(!result, "Unexpected repeated set binding in fixture");
            result = &bound;
        }
    require(result && result->layout && result->set, "Required shader set was not bound to this pass");
    return *result;
}
const rhi::BindingWrite& binding(const tests::BoundSetSnapshot& bound, std::uint32_t index) {
    const rhi::BindingWrite* result{};
    for (const auto& write : bound.writes)
        if (write.binding == index) {
            require(!result, "Duplicate shader binding");
            result = &write;
        }
    require(result, "Required shader binding missing");
    return *result;
}
rhi::Texture* sampled(const tests::BoundSetSnapshot& bound, std::uint32_t index) {
    const auto& write = binding(bound, index);
    require(!write.buffer && write.texture && write.sampler, "Sampled binding lacks a texture/sampler pair");
    require(rhi::contains(write.texture->description().usage, rhi::TextureUsage::Sampled) &&
                write.texture->description().samples == 1,
            "Shader binding is not a single-sample sampled texture");
    return write.texture;
}
rhi::Texture* attachment(const tests::PassSnapshot& recorded, std::uint32_t index, rhi::Format format,
                         std::uint32_t samples, rhi::TextureUsage usage) {
    const auto& desc = recorded.attachmentDescriptions.at(index);
    require(desc.format == format && desc.samples == samples && rhi::contains(desc.usage, usage),
            "Attachment format, sample count or usage violates the pass contract");
    return recorded.framebuffer.attachments.at(index);
}
struct SceneAttachments {
    rhi::Texture* color{};
    rhi::Texture* depth{};
    rhi::Texture* motion{};
    rhi::Texture* resolve{};
};
SceneAttachments sceneAttachments(const tests::FakeCommands& commands, tests::FakeDevice& device, bool temporal,
                                  std::uint32_t samples = 1) {
    const auto& scene = pass(commands, 0);
    require(scene.description.colors == (temporal ? std::vector<std::uint32_t>{0, 2} : std::vector<std::uint32_t>{0}) &&
                scene.description.depth == 1u &&
                scene.description.resolves ==
                    (samples > 1 ? std::vector<std::uint32_t>{2} : std::vector<std::uint32_t>{}),
            "Scene color/depth/motion/resolve slots changed");
    require(scene.framebuffer.attachments.size() == (temporal || samples > 1 ? 3u : 2u),
            "Scene framebuffer attachment count changed");
    SceneAttachments result;
    result.color = attachment(scene, 0, rhi::Format::Rgba8Unorm, samples, rhi::TextureUsage::ColorAttachment);
    result.depth = attachment(scene, 1, device.caps.depthFormat, samples, rhi::TextureUsage::DepthAttachment);
    require(result.color != result.depth, "Scene color and depth alias");
    if (temporal)
        result.motion = attachment(scene, 2, rhi::Format::Rgba32Float, 1,
                                   rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    if (samples > 1)
        result.resolve = attachment(scene, 2, rhi::Format::Rgba8Unorm, 1,
                                    rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    if (temporal || samples > 1)
        require(scene.framebuffer.attachments[2] != result.color && scene.framebuffer.attachments[2] != result.depth,
                "Scene motion/resolve aliases a primary attachment");
    require(scene.sets.size() == 2, "Scene did not bind uniform and material sets");
    const auto& uniformSet = set(scene, 0);
    const auto& uniform = binding(uniformSet, 0);
    require(uniformSet.writes.size() == 1 && uniform.buffer == device.uniform && !uniform.texture && !uniform.sampler &&
                rhi::contains(uniform.buffer->description().usage, rhi::BufferUsage::Uniform),
            "Scene uniform set points at the wrong resource");
    const auto& material = set(scene, 1);
    require(material.layout == uniformSet.layout && material.writes.size() == 2,
            "Scene material set uses the wrong pipeline layout or bindings");
    const auto albedo = sampled(material, 0), normal = sampled(material, 1);
    require(albedo != normal && albedo->description().format == rhi::Format::Rgba8Srgb &&
                normal->description().format == rhi::Format::Rgba8Unorm,
            "Bound material albedo/normal identities or color spaces changed");
    return result;
}
rhi::Texture* fxaaOutput(const tests::FakeCommands& commands, std::size_t index, rhi::Texture* source) {
    const auto& fxaa = pass(commands, index);
    require(fxaa.description.colors == std::vector<std::uint32_t>{0} && !fxaa.description.depth &&
                fxaa.description.resolves.empty() && fxaa.framebuffer.attachments.size() == 1 && fxaa.sets.size() == 1,
            "FXAA pass attachments or sets changed");
    const auto& input = set(fxaa, 0);
    require(input.writes.size() == 1 && sampled(input, 0) == source, "FXAA sampled the wrong source texture");
    auto output = attachment(fxaa, 0, rhi::Format::Rgba8Unorm, 1,
                             rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
    require(output != source, "FXAA reads its own output");
    return output;
}
struct TemporalAttachments {
    rhi::Texture* previousColor{};
    rhi::Texture* previousDepth{};
    rhi::Texture* currentColor{};
    rhi::Texture* currentDepth{};
    bool operator==(const TemporalAttachments&) const = default;
};
TemporalAttachments temporalAttachments(const tests::FakeCommands& commands, tests::FakeDevice& device,
                                        rhi::Texture* finalOutput) {
    const auto scene = sceneAttachments(commands, device, true);
    const auto& taa = pass(commands, 1);
    require(taa.description.colors == std::vector<std::uint32_t>{0, 1} && !taa.description.depth &&
                taa.description.resolves.empty() && taa.framebuffer.attachments.size() == 2 && taa.sets.size() == 1,
            "TAA pass color/depth-history outputs or sets changed");
    const auto& input = set(taa, 0);
    require(input.writes.size() == 4 && sampled(input, 0) == scene.color && sampled(input, 1) == scene.motion,
            "TAA did not sample scene color and motion");
    TemporalAttachments result{
        sampled(input, 2), sampled(input, 3),
        attachment(taa, 0, rhi::Format::Rgba16Float, 1,
                   rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled),
        attachment(taa, 1, rhi::Format::R32Float, 1, rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled)};
    require(result.previousColor->description().format == rhi::Format::Rgba16Float &&
                result.previousDepth->description().format == rhi::Format::R32Float &&
                result.currentColor != result.currentDepth && result.previousColor != result.previousDepth,
            "TAA history color/depth resources changed");
    for (std::uint32_t i = 0; i < 4; ++i) {
        const auto texture = sampled(input, i);
        require(texture != result.currentColor && texture != result.currentDepth, "TAA sampled a current output");
    }
    require(result.previousColor != scene.color && result.previousColor != scene.motion &&
                result.previousDepth != scene.color && result.previousDepth != scene.motion,
            "TAA history aliases scene inputs");
    require(fxaaOutput(commands, 2, result.currentColor) == finalOutput, "Final output does not use FXAA framebuffer");
    return result;
}
}
int main(int argc, char** argv) {
    if (argc != 3) return 1;
    try {
        using namespace aeg;
        tests::FakeDevice device;
        render::Renderer renderer(device, argv[1]);
        renderer.resizeViewport(17, 13);
        assets::ModelData model;
        model.materials.emplace_back();
        assets::MeshData mesh;
        mesh.vertices.resize(3);
        mesh.indices = {0, 1, 2};
        model.meshes.push_back(mesh);
        renderer.uploadModel(model);
        require(device.uploadedFormats == std::vector{rhi::Format::Rgba8Srgb, rhi::Format::Rgba8Unorm},
                "Albedo/normal color spaces changed");
        const std::filesystem::path capture(argv[2]);
        rejects([&] { renderer.captureViewport(capture); }, "Capture accepted unrendered output");
        renderer.setAntialiasing({8, true, true});
        require(renderer.antialiasing().msaaSamples == 1, "TAA must select single sampling");
        render::RenderSettings settings;
        tests::FakeCommands first;
        renderer.recordFrame(first, settings);
        require(first.passes == std::vector<std::string>{"scene", "taa", "fxaa"}, "RHI pass ordering changed");
        require(first.indexedDraws == 1, "Model draw bypassed command interface");
        const auto initial = temporalAttachments(first, device, renderer.outputTexture().get());
        require(pass(first, 0).framebuffer.extent == rhi::Extent2D{17, 13}, "Scene framebuffer extent changed");
        require(renderer.temporalFrameCount() == 0, "Recording advanced unsubmitted TAA");
        rejects([&] { renderer.captureViewport(capture); }, "Pending first frame became capturable");
        const auto uniform = device.uniform->bytes;
        const auto pending = renderer.outputTexture();
        tests::FakeCommands retry;
        renderer.recordFrame(retry, settings);
        require(temporalAttachments(retry, device, renderer.outputTexture().get()) == initial,
                "Unsubmitted retry advanced temporal read/write resources");
        require(device.uniform->bytes == uniform, "Discarded recording advanced jitter");
        renderer.frameSubmitted();
        require(renderer.temporalFrameCount() == 1, "Successful submit did not advance history");
        renderer.frameSubmitted();
        require(renderer.temporalFrameCount() == 1, "Duplicate notification advanced history");
        renderer.captureViewport(capture);
        require(device.lastReadback == pending.get(), "Capture did not use submitted final output");
        renderer.resizeViewport(17, 13);
        require(renderer.temporalFrameCount() == 1, "Identical extent reset history");
        device.failTexture = true;
        rejects([&] { renderer.setAntialiasing({1, false, true}); }, "Allocation failure accepted");
        require(renderer.antialiasing().fxaa && renderer.temporalFrameCount() == 1,
                "Failed AA replacement changed state");
        renderer.captureViewport(capture);
        require(device.lastReadback == pending.get(), "Failed replacement changed output");
        tests::FakeCommands second;
        renderer.recordFrame(second, settings);
        const auto advanced = temporalAttachments(second, device, renderer.outputTexture().get());
        require(advanced.previousColor == initial.currentColor && advanced.previousDepth == initial.currentDepth &&
                    advanced.currentColor == initial.previousColor && advanced.currentDepth == initial.previousDepth,
                "Successful submission did not ping-pong temporal color/depth resources");
        require(sampled(set(pass(first, 2), 0), 0) == initial.currentColor,
                "Later descriptor update changed an earlier binding snapshot");
        renderer.resetTemporalHistory();
        require(renderer.temporalFrameCount() == 0, "Explicit reset retained history");
        renderer.captureViewport(capture);
        require(device.lastReadback == pending.get(), "Reset discarded displayed pixels");
        tests::FakeCommands reset;
        renderer.recordFrame(reset, settings);
        require(temporalAttachments(reset, device, renderer.outputTexture().get()) == initial,
                "Temporal reset selected the wrong history or feedback output");
        renderer.resizeViewport(19, 11);
        rejects([&] { renderer.captureViewport(capture); }, "Resize retained stale capture");
        renderer.setAntialiasing({4, true, false});
        tests::FakeCommands msaa;
        renderer.recordFrame(msaa, settings);
        require(msaa.passes == std::vector<std::string>{"scene", "fxaa"}, "MSAA/FXAA pass ordering changed");
        const auto multisampled = sceneAttachments(msaa, device, false, 4);
        require(fxaaOutput(msaa, 1, multisampled.resolve) == renderer.outputTexture().get(),
                "MSAA final output does not use FXAA framebuffer");
        renderer.frameSubmitted();
        renderer.setAntialiasing({1, true, false});
        tests::FakeCommands singleFxaa;
        renderer.recordFrame(singleFxaa, settings);
        require(singleFxaa.passes == std::vector<std::string>{"scene", "fxaa"},
                "Single-sample FXAA pass ordering changed");
        const auto single = sceneAttachments(singleFxaa, device, false);
        require(fxaaOutput(singleFxaa, 1, single.color) == renderer.outputTexture().get(),
                "Single-sample final output does not use FXAA framebuffer");
        renderer.frameSubmitted();
        renderer.setAntialiasing({1, false, false});
        tests::FakeCommands scene;
        renderer.recordFrame(scene, settings);
        require(scene.passes == std::vector<std::string>{"scene"}, "Disabled AA emitted postprocess");
        require(sceneAttachments(scene, device, false).color == renderer.outputTexture().get(),
                "Plain single-sample output is not the scene color attachment");
        renderer.frameSubmitted();
        require(renderer.temporalFrameCount() == 0, "Non-temporal frame advanced history");
        std::cout << "PASS backend-independent Renderer commands and temporal submission contracts\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
