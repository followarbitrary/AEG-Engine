#include "VulkanRendererImpl.hpp"

namespace aeg::render {
namespace {
std::vector<std::uint32_t> readShader(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open shader: " + foundation::pathToUtf8(path));
    const auto length = input.tellg();
    if (length <= 0 || static_cast<std::size_t>(length) % 4 != 0) throw std::runtime_error("Invalid SPIR-V shader");
    std::vector<std::uint32_t> result(static_cast<std::size_t>(length) / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(result.data()), length);
    if (!input) throw std::runtime_error("Cannot read SPIR-V shader");
    return result;
}
}

VkPipeline VulkanRenderer::Impl::createPipeline(VkRenderPass renderPass, VkPipelineLayout layout, VkSampleCountFlagBits samples,
                          bool scene, const char* vertexName, const char* fragmentName,
                          std::uint32_t colorAttachmentCount) {
    const auto vertexCode = readShader(shaderDirectory / vertexName),
               fragmentCode = readShader(shaderDirectory / fragmentName);
    VkPipeline result{};
    VkShaderModule vertex{}, fragment{};
    auto create = [&](const std::vector<std::uint32_t>& code, VkShaderModule& output) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.size() * 4;
        info.pCode = code.data();
        check(vkCreateShaderModule(device, &info, nullptr, &output), "vkCreateShaderModule");
    };
    create(vertexCode, vertex);
    try {
        create(fragmentCode, fragment);
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        stages[1].pName = "main";
        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription, 3> attributes{
            {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex, position))},
             {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex, normal))},
             {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(Vertex, uv))}}};
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = scene ? 1u : 0u;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = scene ? 3u : 0u;
        input.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = samples;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = scene ? VK_TRUE : VK_FALSE;
        depth.depthWriteEnable = scene ? VK_TRUE : VK_FALSE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                    VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        const std::array blendAttachments{attachment, attachment};
        blend.attachmentCount = colorAttachmentCount;
        blend.pAttachments = blendAttachments.data();
        const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates.data();
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2;
        info.pStages = stages.data();
        info.pVertexInputState = &input;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = layout;
        info.renderPass = renderPass;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &result),
              "vkCreateGraphicsPipelines");
    } catch (...) {
        if (result) vkDestroyPipeline(device, result, nullptr);
        if (fragment) vkDestroyShaderModule(device, fragment, nullptr);
        vkDestroyShaderModule(device, vertex, nullptr);
        throw;
    }
    vkDestroyShaderModule(device, fragment, nullptr);
    vkDestroyShaderModule(device, vertex, nullptr);
    return result;
}

ScenePipeline& VulkanRenderer::Impl::scenePipeline(std::uint32_t samples, bool temporal) {
    auto& cached = temporal ? temporalScene : scenePipelines[std::countr_zero(samples)];
    if (!cached) {
        auto candidate = std::make_unique<ScenePipeline>();
        candidate->device = device;
        candidate->samples = static_cast<VkSampleCountFlagBits>(samples);
        candidate->temporal = temporal;
        createScenePass(*candidate);
        candidate->pipeline = createPipeline(candidate->pass, pipelineLayout, candidate->samples,
            true, temporal ? "model_taa.vert.spv" : "model.vert.spv",
            temporal ? "model_taa.frag.spv" : "model.frag.spv", temporal ? 2u : 1u);
        cached = std::move(candidate);
    }
    return *cached;
}
}
