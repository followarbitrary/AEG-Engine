#include "RendererImpl.hpp"

namespace aeg::render {
namespace {
math::Mat4 backendProjection(const RenderSettings& settings) {
    if (settings.clipSpace.y_down) return settings.viewProjection;
    auto reflection = math::identity();
    reflection[1][1] = -1;
    return reflection * settings.viewProjection;
}
}
void Renderer::Impl::createScenePass(ScenePipeline& target) {
    const bool multisampled = target.samples > 1;
    rhi::RenderPassDesc desc;
    desc.attachments = {{sceneFormat, target.samples, rhi::LoadOp::Clear,
                         multisampled ? rhi::StoreOp::Discard : rhi::StoreOp::Store, rhi::ResourceState::Undefined,
                         multisampled ? rhi::ResourceState::ColorAttachment : rhi::ResourceState::Sampled},
                        {device.capabilities().depthFormat, target.samples, rhi::LoadOp::Clear, rhi::StoreOp::Discard,
                         rhi::ResourceState::Undefined, rhi::ResourceState::DepthAttachment}};
    desc.colors = {0};
    desc.depth = 1;
    if (multisampled || target.temporal) {
        desc.attachments.push_back({target.temporal ? motionFormat : sceneFormat, 1,
                                    target.temporal ? rhi::LoadOp::Clear : rhi::LoadOp::Discard, rhi::StoreOp::Store,
                                    rhi::ResourceState::Undefined, rhi::ResourceState::Sampled});
        if (multisampled)
            desc.resolves = {2};
        else
            desc.colors.push_back(2);
    }
    target.pass = device.createRenderPass(desc);
}

SceneUniform Renderer::Impl::sceneUniform(const RenderSettings& settings) {
    SceneUniform uniform;
    const auto currentProjection = backendProjection(settings);
    uniform.viewProjection = currentProjection;
    uniform.model = settings.model;
    uniform.currentViewProjection = uniform.viewProjection;
    uniform.previousViewProjection = uniform.viewProjection;
    uniform.previousModel = settings.model;
    uniform.clipConvention = {1, settings.clipSpace.reversed_z ? 1.0f : 0.0f, 1,
                              settings.clipSpace.reversed_z ? 1.0f : 0.0f};
    auto light = settings.lightDirection;
    if (aeg::math::dot(light, light) < 0.000001f) light = {0, -1, 0};
    uniform.lightDirection = aeg::math::Vec4(light, 0);
    uniform.parameters = {
        std::clamp(settings.ambient, 0.0f, 1.0f), settings.unlit ? 1.0f : 0.0f, settings.normalMapping ? 1.0f : 0.0f,
        std::isfinite(settings.normalStrength) ? std::clamp(settings.normalStrength, 0.0f, 4.0f) : 1.0f};
    if (aa.taa) {
        const bool perspective = perspectiveProjection(settings.viewProjection);
        if (recordingHistory.valid && (settings.clipSpace != recordingHistory.previous.clipSpace ||
                                       settings.unlit != recordingHistory.previous.unlit ||
                                       settings.normalMapping != recordingHistory.previous.normalMapping ||
                                       settings.normalStrength != recordingHistory.previous.normalStrength ||
                                       settings.ambient != recordingHistory.previous.ambient ||
                                       settings.lightDirection != recordingHistory.previous.lightDirection ||
                                       settings.background != recordingHistory.previous.background ||
                                       perspective != perspectiveProjection(recordingHistory.previous.viewProjection)))
            recordingHistory = TemporalHistory{};
        const auto halton = [](std::uint32_t index, std::uint32_t base) {
            float value = 0, fraction = 1;
            while (index) {
                fraction /= static_cast<float>(base);
                value += fraction * static_cast<float>(index % base);
                index /= base;
            }
            return value;
        };
        const aeg::math::Vec2 inverse{1.0f / static_cast<float>(viewport->width),
                                1.0f / static_cast<float>(viewport->height)};
        auto jitter = aeg::math::identity();
        jitter[3][0] = 2 * (halton(recordingHistory.jitterIndex + 1, 2) - 0.5f) * inverse.x;
        jitter[3][1] = 2 * (halton(recordingHistory.jitterIndex + 1, 3) - 0.5f) * inverse.y;
        uniform.viewProjection = jitter * currentProjection;
        uniform.currentViewProjection = currentProjection;
        uniform.previousViewProjection =
            recordingHistory.valid ? backendProjection(recordingHistory.previous) : currentProjection;
        uniform.previousModel = recordingHistory.valid ? recordingHistory.previous.model : settings.model;
        uniform.clipConvention.w = (recordingHistory.valid ? recordingHistory.previous.clipSpace.reversed_z
                                                          : settings.clipSpace.reversed_z) ? 1.0f : 0.0f;
        uniform.temporalInfo = {
            inverse.x, inverse.y, perspective ? 1.0f : 0.0f,
            (recordingHistory.valid ? perspectiveProjection(recordingHistory.previous.viewProjection) : perspective)
                ? 1.0f
                : 0.0f};
    }
    return uniform;
}

void Renderer::Impl::drawScene(rhi::CommandList& commands, const RenderSettings& settings) {
    auto& scene = scenePipeline(aa.msaaSamples, aa.taa, settings.clipSpace.reversed_z);
    const auto uniform = sceneUniform(settings);
    std::memcpy(uniformBuffer->mapped().data(), &uniform, sizeof(uniform));
    std::array<rhi::ClearValue, 3> clears{};
    clears[0].color = {settings.background.x, settings.background.y, settings.background.z, 1};
    clears[1].depth = settings.clipSpace.reversed_z ? 0.0f : 1.0f;
    clears[2].color = {0, 0, -1, -1};
    commands.beginRenderPass(*scene.pass, *viewport->sceneFrame, viewport->extent(),
                             std::span(clears).first(aa.taa ? 3u : 2u));
    commands.setViewport(viewport->extent());
    commands.bindPipeline(*(settings.clipSpace.reversed_z ? scene.greater : scene.less));
    commands.bindSet(*pipelineLayout, 0, *sceneDescriptor);
    if (model)
        for (auto& mesh : model->meshes) {
            auto& material = model->materials[mesh.material];
            commands.bindVertexBuffer(*mesh.vertices);
            commands.bindIndexBuffer(*mesh.indices);
            commands.bindSet(*pipelineLayout, 1, *material.descriptor);
            commands.pushConstants(*pipelineLayout, std::as_bytes(std::span(&material.parameters, 1)));
            commands.drawIndexed(mesh.indexCount);
        }
    commands.endRenderPass();
}
}
