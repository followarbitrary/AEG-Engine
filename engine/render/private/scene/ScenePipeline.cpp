#include "RendererImpl.hpp"

namespace aeg::render {
std::unique_ptr<rhi::GraphicsPipeline>
Renderer::Impl::createPipeline(rhi::RenderPass& pass, rhi::PipelineLayout& layout, std::uint32_t samples, bool scene,
                               detail::ShaderId vertexId, detail::ShaderId fragmentId,
                               std::uint32_t colorAttachmentCount, rhi::DepthCompare depthCompare) {
    const auto vertexCode = detail::embeddedShader(vertexId);
    const auto fragmentCode = detail::embeddedShader(fragmentId);
    auto vertex = device.createShader({rhi::ShaderStage::Vertex, rhi::ShaderFormat::Spirv, vertexCode});
    auto fragment = device.createShader({rhi::ShaderStage::Fragment, rhi::ShaderFormat::Spirv, fragmentCode});
    rhi::GraphicsPipelineDesc desc;
    desc.pass = &pass;
    desc.layout = &layout;
    desc.vertex = vertex.get();
    desc.fragment = fragment.get();
    desc.samples = samples;
    desc.colorAttachmentCount = colorAttachmentCount;
    desc.depthTest = desc.depthWrite = scene;
    desc.depthCompare = depthCompare;
    if (scene) {
        desc.vertexStride = sizeof(Vertex);
        desc.attributes = {{0, static_cast<std::uint32_t>(offsetof(Vertex, position)), rhi::VertexFormat::Float3},
                           {1, static_cast<std::uint32_t>(offsetof(Vertex, normal)), rhi::VertexFormat::Float3},
                           {2, static_cast<std::uint32_t>(offsetof(Vertex, uv)), rhi::VertexFormat::Float2},
                           {3, static_cast<std::uint32_t>(offsetof(Vertex, tangent)), rhi::VertexFormat::Float4}};
    }
    return device.createGraphicsPipeline(desc);
}

ScenePipeline& Renderer::Impl::scenePipeline(std::uint32_t samples, bool temporal, bool reversed) {
    auto& cached = temporal ? temporalScene : scenePipelines[std::countr_zero(samples)];
    if (!cached) {
        auto candidate = std::make_unique<ScenePipeline>();
        candidate->samples = samples;
        candidate->temporal = temporal;
        createScenePass(*candidate);
        cached = std::move(candidate);
    }
    auto& pipeline = reversed ? cached->greater : cached->less;
    if (!pipeline)
        pipeline = createPipeline(
            *cached->pass, *pipelineLayout, samples, true,
            temporal ? detail::ShaderId::ModelTaaVertex : detail::ShaderId::ModelVertex,
            temporal ? detail::ShaderId::ModelTaaFragment : detail::ShaderId::ModelFragment, temporal ? 2u : 1u,
            reversed ? rhi::DepthCompare::Greater : rhi::DepthCompare::Less);
    return *cached;
}
}
