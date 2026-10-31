#include "RendererImpl.hpp"
#include "shadow/ShadowFitting.hpp"

namespace aeg::render {

void Renderer::Impl::initializeShadowSupport() {
    const auto& caps = device.capabilities();
    constexpr auto usage = rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled;
    for (const auto format : {rhi::Format::D32Float, rhi::Format::D16Unorm, rhi::Format::D24UnormS8}) {
        const auto support = device.textureSupport(format, usage);
        if (!support.supported || !(support.sampleMask & 1u)) continue;
        const auto maximum = std::min({4096u, caps.maximumImageDimension, caps.maximumFramebufferDimension,
                                       support.maximumExtent.width, support.maximumExtent.height});
        if (maximum < 16) continue;
        shadowFormat = format;
        maximumShadow = std::bit_floor(maximum);
        shadowSettings.resolution = std::min(shadowSettings.resolution, maximumShadow);
        shadowSupported = true;
        break;
    }
}

void Renderer::Impl::setShadows(const ShadowSettings& requested) {
    if (requested.lightIndex >= kMaxLights || requested.resolution < 16 || requested.resolution > 4096 ||
        !std::has_single_bit(requested.resolution) || !std::isfinite(requested.depthBias) || requested.depthBias < 0 ||
        requested.depthBias > 1 || !std::isfinite(requested.slopeBias) || requested.slopeBias < 0 ||
        requested.slopeBias > 1 || requested.cascadeCount < 1 || requested.cascadeCount > kMaxShadowCascades ||
        !std::isfinite(requested.splitLambda) || requested.splitLambda < 0 || requested.splitLambda > 1 ||
        !std::isfinite(requested.maxDistance) || requested.maxDistance < 0 || !std::isfinite(requested.blendFraction) ||
        requested.blendFraction < 0 || requested.blendFraction > 0.3f ||
        !std::isfinite(requested.distanceFadeFraction) || requested.distanceFadeFraction < 0 ||
        requested.distanceFadeFraction > 0.5f || requested.pcfRadius > 2)
        throw std::invalid_argument("Invalid shadow light, resolution, bias, cascades or filter settings");
    if (requested.enabled && !shadowSupported) throw std::runtime_error("Sampled depth shadows are unsupported");
    if (shadowSupported && requested.resolution > maximumShadow)
        throw std::invalid_argument("Shadow resolution exceeds device capability");
    if (requested == shadowSettings) return;
    std::unique_ptr<ShadowResources> replacement;
    if (requested.enabled &&
        (!shadow || shadow->resolution != requested.resolution || shadow->capacity != requested.cascadeCount)) {
        replacement = std::make_unique<ShadowResources>();
        replacement->resolution = requested.resolution;
        replacement->capacity = requested.cascadeCount;
        const rhi::Extent2D extent{requested.resolution, requested.resolution};
        // The graph transitions each cascade into the depth attachment state,
        // then publishes Sampled after the pass completes.
        replacement->pass = device.createRenderPass({{{shadowFormat, 1, rhi::LoadOp::Clear, rhi::StoreOp::Store,
                                                       rhi::ResourceState::DepthAttachment,
                                                       rhi::ResourceState::DepthAttachment}},
                                                     {},
                                                     0,
                                                     {}});
        for (std::uint32_t i = 0; i < replacement->capacity; ++i) {
            replacement->textures[i] = device.createTexture(
                {extent, shadowFormat, rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled, 1});
            replacement->framebuffers[i] =
                device.createFramebuffer({replacement->pass.get(), {replacement->textures[i].get()}, extent});
        }
        replacement->pipeline = createPipeline(*replacement->pass, *shadowPipelineLayout, 1, true,
                                               detail::ShaderId::ShadowVertex, detail::ShaderId::ShadowFragment, 0);
        // Spot renders only map zero even with capacity four. Immutable variants
        // keep every other statically used sampler on initialized white.
        for (std::uint32_t active = 1; active <= replacement->capacity; ++active) {
            std::array<rhi::BindingWrite, kMaxShadowCascades> writes;
            for (std::uint32_t i = 0; i < kMaxShadowCascades; ++i)
                writes[i] = {i, nullptr, i < active ? replacement->textures[i].get() : fallbackShadowTexture.get(),
                             pointSampler.get()};
            replacement->descriptors[active - 1] = device.createBindingSet(*shadowSetLayout, writes);
        }
        device.waitIdle();
    }
    // All candidate allocation, compilation, descriptor creation and waiting
    // succeeded. Publishing and resetting history below cannot throw.
    if (replacement) shadow.swap(replacement);
    shadowSettings = requested;
    resetTemporalHistory();
}

void Renderer::Impl::configureShadowUniform(SceneUniform& uniform, const RenderSettings& settings) {
    uniform.lightViewProjections.fill(math::identity());
    uniform.cascadeSplits = {};
    uniform.cascadeBlendStarts = {};
    uniform.shadowInfo = {0, static_cast<float>(shadowSettings.lightIndex),
                          1.0f / static_cast<float>(shadowSettings.resolution), 0};
    uniform.shadowBias = {shadowSettings.depthBias, shadowSettings.slopeBias,
                          static_cast<float>(shadowSettings.pcfRadius), 0};
    if (!shadowSettings.enabled || !shadow || !model ||
        static_cast<float>(shadowSettings.lightIndex) >= uniform.lightInfo.x)
        return;
    // Use exactly the sanitized record consumed by direct lighting.
    const auto& light = uniform.lights[shadowSettings.lightIndex];
    if (light.radiance.w < 0.5f || (light.directionType.w != static_cast<float>(LightType::Directional) &&
                                    light.directionType.w != static_cast<float>(LightType::Spot)))
        return;
    ShadowFit fit;
    if (!tryFitShadow(settings, shadowSettings, model->minimum, model->maximum, light, fit) || fit.count == 0 ||
        fit.count > shadow->capacity)
        return;
    uniform.lightViewProjections = fit.matrices;
    uniform.cascadeSplits = fit.splits;
    uniform.cascadeBlendStarts = fit.blendStarts;
    uniform.shadowInfo.x = 1;
    uniform.shadowInfo.w = static_cast<float>(fit.count);
}

void Renderer::Impl::drawShadow(rhi::CommandList& commands, const SceneUniform& uniform, std::uint32_t cascade) {
    if (uniform.shadowInfo.x < 0.5f || !shadow || !model || cascade >= static_cast<std::uint32_t>(uniform.shadowInfo.w))
        return;
    const rhi::Extent2D extent{shadow->resolution, shadow->resolution};
    commands.setViewport(extent);
    commands.bindPipeline(*shadow->pipeline);
    commands.bindSet(*shadowPipelineLayout, 0, *activeSceneDescriptor);
    for (const auto& mesh : model->meshes) {
        const auto& material = model->materials[mesh.material];
        if (material.parameters.alphaMode == static_cast<std::uint32_t>(assets::AlphaMode::Blend)) continue;
        commands.bindVertexBuffer(*mesh.vertices);
        commands.bindIndexBuffer(*mesh.indices);
        commands.bindSet(*shadowPipelineLayout, 1, *material.descriptor);
        const ShadowPush push{material.parameters, cascade, {}};
        commands.pushConstants(*shadowPipelineLayout, std::as_bytes(std::span(&push, 1)));
        commands.drawIndexed(mesh.indexCount);
    }
}
}
