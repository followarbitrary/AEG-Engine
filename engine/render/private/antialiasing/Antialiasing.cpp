#include "VulkanRendererImpl.hpp"

namespace aeg::render {
void VulkanRenderer::Impl::createPostPass() {
    VkAttachmentDescription color{};
    color.format = sceneFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference output{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &output;
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    // Global scopes: FXAA reads neighboring texels and ImGui scales the result.
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
    info.pDependencies = dependencies.data();
    check(vkCreateRenderPass(device, &info, nullptr, &postPass), "vkCreateRenderPass(FXAA)");
    if (temporalSupported) {
        std::array<VkAttachmentDescription, 2> attachments{color, color};
        attachments[0].format = historyFormat;
        attachments[1].format = historyDepthFormat;
        const std::array<VkAttachmentReference, 2> outputs{{
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}}};
        subpass.colorAttachmentCount = static_cast<std::uint32_t>(outputs.size());
        subpass.pColorAttachments = outputs.data();
        info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        check(vkCreateRenderPass(device, &info, nullptr, &temporalPass), "vkCreateRenderPass(TAA)");
    }
}

void VulkanRenderer::Impl::setAntialiasing(const AntialiasingSettings& requested) {
    if (requested.msaaSamples != 1 && requested.msaaSamples != 2 &&
        requested.msaaSamples != 4 && requested.msaaSamples != 8)
        throw std::invalid_argument("MSAA sample count must be 1, 2, 4 or 8");
    if (requested.taa && !temporalSupported)
        throw std::runtime_error("TAA requires sampled RGBA32F motion, RGBA16F color and R32F depth attachments");
    AntialiasingSettings effective{1, requested.fxaa, requested.taa};
    if (!requested.taa)
        for (const auto samples : {8u, 4u, 2u, 1u})
            if (samples <= requested.msaaSamples && (sampleMask & samples)) {
                effective.msaaSamples = samples;
                break;
            }
    if (effective == aa) return;
    check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(antialiasing)");
    const auto maximum = effective.taa ? maximumTemporalViewport : maximumViewport;
    auto replacement = createViewport(std::min(viewport->width, maximum), std::min(viewport->height, maximum), effective);
    viewport.swap(replacement);
    aa = effective;
    sceneRendered = false;
    resetTemporalHistory();
}
}
