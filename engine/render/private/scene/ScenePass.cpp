#include "RendererImpl.hpp"

namespace aeg::render {
namespace {
math::Mat4 backendProjection(const RenderSettings& settings) {
    if (settings.clipSpace.y_down) return settings.viewProjection;
    auto reflection = math::identity();
    reflection[1][1] = -1;
    return reflection * settings.viewProjection;
}
GpuLight packLight(const Light& light) {
    GpuLight result;
    const bool validType =
        light.type == LightType::Directional || light.type == LightType::Point || light.type == LightType::Spot;
    const bool validPosition = math::is_finite(light.position);
    const bool validRange = std::isfinite(light.range) && light.range > 0;
    const bool enabled =
        light.enabled && validType && (light.type == LightType::Directional || (validPosition && validRange));
    math::Vec3 direction;
    if (!math::try_normalize(light.direction, direction)) direction = {0, -1, 0};
    result.positionRange = {validPosition ? light.position : math::Vec3{}, validRange ? light.range : 0};
    result.directionType = {direction, validType ? static_cast<float>(light.type) : 0.0f};
    const auto nonnegative = [](float value) { return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f; };
    const float intensity = nonnegative(light.intensity);
    const auto radiance = [&](float component) {
        // Multiply in double so extreme, finite public values cannot overflow first.
        // Punctual source radiance must retain its magnitude until distance attenuation.
        const double maximum = light.type == LightType::Directional ? 65504.0 : std::numeric_limits<float>::max();
        return static_cast<float>(std::min(double(nonnegative(component)) * intensity, maximum));
    };
    result.radiance = {radiance(light.color.x), radiance(light.color.y), radiance(light.color.z),
                       enabled ? 1.0f : 0.0f};
    constexpr float maximumCone = math::kPi * 0.5f - 0.001f;
    const float outer = std::clamp(std::isfinite(light.outerConeAngle) ? light.outerConeAngle : math::to_radians(30),
                                   0.0f, maximumCone);
    const float inner =
        std::clamp(std::isfinite(light.innerConeAngle) ? light.innerConeAngle : math::to_radians(20), 0.0f, outer);
    result.cone = {std::cos(inner), std::cos(outer), 0, 0};
    return result;
}
bool sameLightValue(float left, float right) { return left == right || (std::isnan(left) && std::isnan(right)); }
bool sameLightValue(math::Vec3 left, math::Vec3 right) {
    return sameLightValue(left.x, right.x) && sameLightValue(left.y, right.y) && sameLightValue(left.z, right.z);
}
bool sameLights(const RenderSettings& left, const RenderSettings& right) {
    if (left.lightCount != right.lightCount) return false;
    const auto count = std::min(left.lightCount, kMaxLights);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& a = left.lights[i];
        const auto& b = right.lights[i];
        if (a.type != b.type || a.enabled != b.enabled || !sameLightValue(a.position, b.position) ||
            !sameLightValue(a.direction, b.direction) || !sameLightValue(a.color, b.color) ||
            !sameLightValue(a.intensity, b.intensity) || !sameLightValue(a.range, b.range) ||
            !sameLightValue(a.innerConeAngle, b.innerConeAngle) || !sameLightValue(a.outerConeAngle, b.outerConeAngle))
            return false;
    }
    return true;
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
    const bool perspective = perspectiveProjection(settings.viewProjection);
    const auto currentProjection = backendProjection(settings);
    uniform.viewProjection = currentProjection;
    uniform.model = settings.model;
    uniform.currentViewProjection = uniform.viewProjection;
    uniform.previousViewProjection = uniform.viewProjection;
    uniform.previousModel = settings.model;
    uniform.clipConvention = {1, settings.clipSpace.reversed_z ? 1.0f : 0.0f, 1,
                              settings.clipSpace.reversed_z ? 1.0f : 0.0f};
    math::Vec3 towardViewer;
    if (!math::try_normalize(settings.cameraTowardViewer, towardViewer)) towardViewer = {0, 0, 1};
    uniform.cameraPosition = math::Vec4(
        math::is_finite(settings.cameraPosition) ? settings.cameraPosition : math::Vec3{}, perspective ? 1.0f : 0.0f);
    uniform.cameraTowardViewer = math::Vec4(towardViewer, 0);
    const auto lightCount = std::min(settings.lightCount, kMaxLights);
    uniform.lightInfo = {static_cast<float>(lightCount), 0, 0, 0};
    for (std::uint32_t i = 0; i < lightCount; ++i) uniform.lights[i] = packLight(settings.lights[i]);
    uniform.parameters = {std::isfinite(settings.ambient) ? std::clamp(settings.ambient, 0.0f, 1.0f) : 0.0f,
                          settings.unlit ? 1.0f : 0.0f, settings.normalMapping ? 1.0f : 0.0f,
                          std::isfinite(settings.normalStrength) ? std::clamp(settings.normalStrength, 0.0f, 4.0f)
                                                                 : 1.0f};
    uniform.environmentInfo = {environmentSettings.enabled && environmentResources ? 1.0f : 0.0f,
                               environmentSettings.intensity, environmentSettings.rotationRadians,
                               environmentResources ? float(environmentResources->maps.prefilterMipCount - 1) : 0.0f};
    if (aa.taa) {
        if (recordingHistory.valid && (settings.clipSpace != recordingHistory.previous.clipSpace ||
                                       settings.unlit != recordingHistory.previous.unlit ||
                                       settings.normalMapping != recordingHistory.previous.normalMapping ||
                                       settings.normalStrength != recordingHistory.previous.normalStrength ||
                                       settings.ambient != recordingHistory.previous.ambient ||
                                       !sameLights(settings, recordingHistory.previous) ||
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
        uniform.clipConvention.w =
            (recordingHistory.valid ? recordingHistory.previous.clipSpace.reversed_z : settings.clipSpace.reversed_z)
                ? 1.0f
                : 0.0f;
        uniform.temporalInfo = {
            inverse.x, inverse.y, perspective ? 1.0f : 0.0f,
            (recordingHistory.valid ? perspectiveProjection(recordingHistory.previous.viewProjection) : perspective)
                ? 1.0f
                : 0.0f};
    }
    return uniform;
}

void Renderer::Impl::drawScene(rhi::CommandList& commands, const RenderSettings& settings,
                               const SceneUniform& uniform) {
    auto& scene = scenePipeline(aa.msaaSamples, aa.taa, settings.clipSpace.reversed_z);
    struct TransparentDraw {
        const GpuMesh* mesh;
        double depth;
    };
    std::vector<TransparentDraw> transparent;
    if (model) {
        const auto transform = uniform.currentViewProjection * settings.model;
        for (const auto& mesh : model->meshes) {
            if (model->materials[mesh.material].parameters.alphaMode !=
                static_cast<std::uint32_t>(assets::AlphaMode::Blend))
                continue;
            const auto clip = transform * math::Vec4(mesh.center, 1);
            double depth = 0;
            if (math::is_finite(clip) && clip.w != 0) {
                depth = static_cast<double>(clip.z) / clip.w;
                if (settings.clipSpace.reversed_z) depth = 1 - depth;
            }
            transparent.push_back({&mesh, depth});
        }
        // Mesh-center sorting is stable for equal depths. Intersecting meshes and
        // overlapping triangles inside one mesh still require more advanced transparency.
        std::stable_sort(transparent.begin(), transparent.end(),
                         [](const auto& left, const auto& right) { return left.depth > right.depth; });
    }
    // Finish allocations and lazy pipeline creation before beginning a command pass.
    if (!transparent.empty()) scenePipeline(aa.msaaSamples, aa.taa, settings.clipSpace.reversed_z, true);
    std::array<rhi::ClearValue, 3> clears{};
    clears[0].color = {settings.background.x, settings.background.y, settings.background.z, 1};
    clears[1].depth = settings.clipSpace.reversed_z ? 0.0f : 1.0f;
    clears[2].color = {0, 0, -1, -1};
    commands.beginRenderPass(*scene.pass, *viewport->sceneFrame, viewport->extent(),
                             std::span(clears).first(aa.taa ? 3u : 2u));
    commands.setViewport(viewport->extent());
    commands.bindPipeline(*(settings.clipSpace.reversed_z ? scene.greater : scene.less));
    commands.bindSet(*pipelineLayout, 0, *sceneDescriptor);
    commands.bindSet(*pipelineLayout, 2,
                     uniform.shadowInfo.x > 0.5f
                         ? *shadow->descriptors[static_cast<std::uint32_t>(uniform.shadowInfo.w) - 1]
                         : *fallbackShadowDescriptor);
    commands.bindSet(*pipelineLayout, 3,
                     environmentResources ? *environmentResources->descriptor : *fallbackEnvironmentDescriptor);
    if (model) {
        const auto drawMesh = [&](const GpuMesh& mesh) {
            const auto& material = model->materials[mesh.material];
            commands.bindVertexBuffer(*mesh.vertices);
            commands.bindIndexBuffer(*mesh.indices);
            commands.bindSet(*pipelineLayout, 1, *material.descriptor);
            commands.pushConstants(*pipelineLayout, std::as_bytes(std::span(&material.parameters, 1)));
            commands.drawIndexed(mesh.indexCount);
        };
        for (const auto& mesh : model->meshes) {
            if (model->materials[mesh.material].parameters.alphaMode !=
                static_cast<std::uint32_t>(assets::AlphaMode::Blend))
                drawMesh(mesh);
        }
        if (!transparent.empty()) {
            commands.bindPipeline(*(settings.clipSpace.reversed_z ? scene.blendGreater : scene.blendLess));
            for (const auto& draw : transparent) drawMesh(*draw.mesh);
        }
    }
    commands.endRenderPass();
}
}
