#include "VulkanBackendInternal.hpp"

namespace aeg::rhi::vulkan::detail {
VulkanShader::~VulkanShader() {
    if (handle) vkDestroyShaderModule(owner->handle, handle, nullptr);
}
VulkanRenderPass::~VulkanRenderPass() {
    if (handle) vkDestroyRenderPass(owner->handle, handle, nullptr);
}
VulkanFramebuffer::~VulkanFramebuffer() {
    if (handle) vkDestroyFramebuffer(owner->handle, handle, nullptr);
}
VulkanPipeline::~VulkanPipeline() {
    if (handle) vkDestroyPipeline(owner->handle, handle, nullptr);
}
std::unique_ptr<Shader> VulkanDevice::createShader(const ShaderDesc& desc) {
    if (desc.format != ShaderFormat::Spirv || desc.bytecode.empty() || desc.bytecode.size() % 4 ||
        (desc.stage != ShaderStage::Vertex && desc.stage != ShaderStage::Fragment))
        throw std::invalid_argument("Invalid SPIR-V shader description");
    std::vector<std::uint32_t> code(desc.bytecode.size() / 4);
    std::memcpy(code.data(), desc.bytecode.data(), desc.bytecode.size());
    if (code.front() != 0x07230203u) throw std::invalid_argument("Invalid SPIR-V magic");
    auto result = std::make_unique<VulkanShader>(*this, desc.stage);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = desc.bytecode.size();
    info.pCode = code.data();
    check(vkCreateShaderModule(handle, &info, nullptr, &result->handle), "vkCreateShaderModule");
    return result;
}
std::unique_ptr<RenderPass> VulkanDevice::createRenderPass(const RenderPassDesc& desc) {
    validate(desc);
    if (desc.colors.size() > caps.maximumColorAttachments) throw std::invalid_argument("Too many color attachments");
    auto result = std::make_unique<VulkanRenderPass>(*this, desc);
    std::vector<VkAttachmentDescription> attachments;
    for (const auto& attachment : desc.attachments) {
        VkAttachmentDescription native{};
        native.format = nativeFormat(attachment.format);
        native.samples = static_cast<VkSampleCountFlagBits>(attachment.samples);
        native.loadOp = attachment.load == LoadOp::Clear  ? VK_ATTACHMENT_LOAD_OP_CLEAR
                        : attachment.load == LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                          : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        native.storeOp =
            attachment.store == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        native.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        native.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        native.initialLayout = imageLayout(attachment.initialState);
        native.finalLayout = imageLayout(attachment.finalState);
        attachments.push_back(native);
    }
    std::vector<VkAttachmentReference> colors, resolves;
    for (auto index : desc.colors) colors.push_back({index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    for (auto index : desc.resolves)
        resolves.push_back(
            {index == unusedAttachment ? VK_ATTACHMENT_UNUSED : index, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    VkAttachmentReference depth{desc.depth.value_or(VK_ATTACHMENT_UNUSED),
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
    subpass.pColorAttachments = colors.data();
    subpass.pResolveAttachments = resolves.empty() ? nullptr : resolves.data();
    subpass.pDepthStencilAttachment = desc.depth ? &depth : nullptr;
    // Global dependencies are deliberate: postprocess and GUI sample at different pixel positions.
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
    info.pDependencies = dependencies.data();
    check(vkCreateRenderPass(handle, &info, nullptr, &result->handle), "vkCreateRenderPass");
    return result;
}
std::unique_ptr<Framebuffer> VulkanDevice::createFramebuffer(const FramebufferDesc& desc) {
    if (!desc.pass || !desc.extent.width || !desc.extent.height ||
        desc.extent.width > properties.limits.maxFramebufferWidth ||
        desc.extent.height > properties.limits.maxFramebufferHeight)
        throw std::invalid_argument("Invalid framebuffer description");
    auto& pass = owned<VulkanRenderPass>(*this, *desc.pass);
    if (desc.attachments.size() != pass.desc.attachments.size())
        throw std::invalid_argument("Framebuffer attachment count mismatch");
    std::vector<VkImageView> views;
    for (std::size_t i = 0; i < desc.attachments.size(); ++i) {
        if (!desc.attachments[i]) throw std::invalid_argument("Null framebuffer attachment");
        auto& texture = owned<VulkanTexture>(*this, *desc.attachments[i]);
        const auto& attachment = pass.desc.attachments[i];
        if (texture.desc.format != attachment.format || texture.desc.samples != attachment.samples ||
            texture.desc.extent.width < desc.extent.width || texture.desc.extent.height < desc.extent.height ||
            !contains(texture.desc.usage,
                      isDepthFormat(attachment.format) ? TextureUsage::DepthAttachment : TextureUsage::ColorAttachment))
            throw std::invalid_argument("Framebuffer attachment format, samples, extent or usage mismatch");
        views.push_back(texture.view);
    }
    auto result = std::make_unique<VulkanFramebuffer>(*this);
    result->pass = &pass;
    result->size = desc.extent;
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = pass.handle;
    info.attachmentCount = static_cast<std::uint32_t>(views.size());
    info.pAttachments = views.data();
    info.width = desc.extent.width;
    info.height = desc.extent.height;
    info.layers = 1;
    check(vkCreateFramebuffer(handle, &info, nullptr, &result->handle), "vkCreateFramebuffer");
    return result;
}
std::unique_ptr<GraphicsPipeline> VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    validate(desc, caps.independentBlend);
    if (!desc.pass || !desc.layout || !desc.vertex || !desc.fragment)
        throw std::invalid_argument("Missing graphics pipeline resource");
    auto& pass = owned<VulkanRenderPass>(*this, *desc.pass);
    auto& layout = owned<VulkanPipelineLayout>(*this, *desc.layout);
    auto& vertex = owned<VulkanShader>(*this, *desc.vertex);
    auto& fragment = owned<VulkanShader>(*this, *desc.fragment);
    if (vertex.stage != ShaderStage::Vertex || fragment.stage != ShaderStage::Fragment ||
        desc.colorAttachmentCount != pass.desc.colors.size() || (desc.depthTest && !pass.desc.depth))
        throw std::invalid_argument("Graphics pipeline/pass mismatch");
    for (auto color : pass.desc.colors)
        if (pass.desc.attachments[color].samples != desc.samples)
            throw std::invalid_argument("Pipeline color sample count mismatch");
    if (pass.desc.depth && pass.desc.attachments[*pass.desc.depth].samples != desc.samples)
        throw std::invalid_argument("Pipeline depth sample count mismatch");
    auto result = std::make_unique<VulkanPipeline>(*this);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex.handle;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment.handle;
    stages[1].pName = "main";
    VkVertexInputBindingDescription binding{0, desc.vertexStride, VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> attributes;
    std::set<std::uint32_t> locations;
    for (const auto& attribute : desc.attributes) {
        VkFormat format{};
        std::uint32_t bytes{};
        switch (attribute.format) {
        case VertexFormat::Float2:
            format = VK_FORMAT_R32G32_SFLOAT;
            bytes = 8;
            break;
        case VertexFormat::Float3:
            format = VK_FORMAT_R32G32B32_SFLOAT;
            bytes = 12;
            break;
        case VertexFormat::Float4:
            format = VK_FORMAT_R32G32B32A32_SFLOAT;
            bytes = 16;
            break;
        }
        if (!locations.insert(attribute.location).second || attribute.offset > desc.vertexStride ||
            bytes > desc.vertexStride - attribute.offset)
            throw std::invalid_argument("Invalid vertex attribute");
        attributes.push_back({attribute.location, 0, format, attribute.offset});
    }
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = desc.vertexStride ? 1u : 0u;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    input.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(desc.samples);
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = desc.depthTest;
    depth.depthWriteEnable = desc.depthWrite;
    switch (desc.depthCompare) {
    case DepthCompare::Less:
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        break;
    case DepthCompare::Greater:
        depth.depthCompareOp = VK_COMPARE_OP_GREATER;
        break;
    default:
        throw std::invalid_argument("Invalid depth comparison");
    }
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(desc.colorAttachmentCount, attachment);
    for (std::size_t i = 0; i < blendAttachments.size(); ++i) {
        if (desc.colorBlends.empty() || desc.colorBlends[i] == BlendMode::Opaque) continue;
        const auto format = pass.desc.attachments[pass.desc.colors[i]].format;
        if (!textureSupport(format, TextureUsage::ColorAttachment).colorBlending)
            throw std::invalid_argument("Color attachment format does not support blending");
        auto& target = blendAttachments[i];
        target.blendEnable = VK_TRUE;
        target.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        target.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        target.colorBlendOp = VK_BLEND_OP_ADD;
        target.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        target.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        target.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = desc.colorAttachmentCount;
    blend.pAttachments = blendAttachments.data();
    const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout.handle;
    info.renderPass = pass.handle;
    check(vkCreateGraphicsPipelines(handle, VK_NULL_HANDLE, 1, &info, nullptr, &result->handle),
          "vkCreateGraphicsPipelines");
    return result;
}
}
