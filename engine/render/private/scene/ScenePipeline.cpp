#include "RendererImpl.hpp"

namespace aeg::render {
namespace {
std::vector<std::byte> readShader(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open shader: " + foundation::pathToUtf8(path));
    const auto length = input.tellg();
    if (length <= 0 || static_cast<std::size_t>(length) % 4 != 0) throw std::runtime_error("Invalid SPIR-V shader");
    std::vector<std::byte> result(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(result.data()), length);
    if (!input) throw std::runtime_error("Cannot read SPIR-V shader");
    return result;
}
}

std::unique_ptr<rhi::GraphicsPipeline>
Renderer::Impl::createPipeline(rhi::RenderPass& pass, rhi::PipelineLayout& layout, std::uint32_t samples, bool scene,
                               const char* vertexName, const char* fragmentName, std::uint32_t colorAttachmentCount) {
    const auto vertexCode = readShader(shaderDirectory / vertexName);
    const auto fragmentCode = readShader(shaderDirectory / fragmentName);
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
    if (scene) {
        desc.vertexStride = sizeof(Vertex);
        desc.attributes = {{0, static_cast<std::uint32_t>(offsetof(Vertex, position)), rhi::VertexFormat::Float3},
                           {1, static_cast<std::uint32_t>(offsetof(Vertex, normal)), rhi::VertexFormat::Float3},
                           {2, static_cast<std::uint32_t>(offsetof(Vertex, uv)), rhi::VertexFormat::Float2},
                           {3, static_cast<std::uint32_t>(offsetof(Vertex, tangent)), rhi::VertexFormat::Float4}};
    }
    return device.createGraphicsPipeline(desc);
}

ScenePipeline& Renderer::Impl::scenePipeline(std::uint32_t samples, bool temporal) {
    auto& cached = temporal ? temporalScene : scenePipelines[std::countr_zero(samples)];
    if (!cached) {
        auto candidate = std::make_unique<ScenePipeline>();
        candidate->samples = samples;
        candidate->temporal = temporal;
        createScenePass(*candidate);
        candidate->pipeline = createPipeline(*candidate->pass, *pipelineLayout, samples, true,
                                             temporal ? "model_taa.vert.spv" : "model.vert.spv",
                                             temporal ? "model_taa.frag.spv" : "model.frag.spv", temporal ? 2u : 1u);
        cached = std::move(candidate);
    }
    return *cached;
}
}
