#include "VulkanRendererImpl.hpp"

namespace aeg::render {
void VulkanRenderer::Impl::createScenePass(ScenePipeline& target) {
    const bool multisampled = target.samples != VK_SAMPLE_COUNT_1_BIT;
    std::array<VkAttachmentDescription, 3> attachments{};
    attachments[0].format = sceneFormat;
    attachments[0].samples = target.samples;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = multisampled ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                             : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1].format = depthFormat;
    attachments[1].samples = target.samples;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[2] = attachments[0];
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = target.temporal ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].format = target.temporal ? motionFormat : sceneFormat;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
        resolve{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const std::array colors{color, resolve};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = target.temporal ? 2u : 1u;
    subpass.pColorAttachments = colors.data();
    subpass.pDepthStencilAttachment = &depth;
    subpass.pResolveAttachments = multisampled ? &resolve : nullptr;
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // The sampled scene is drawn at a different offset/scale in ImGui.
    // Framebuffer-local dependencies would not synchronize those texels.
    dependencies[0].dependencyFlags = 0;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = 0;
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = (multisampled || target.temporal) ? 3u : 2u;
    info.pAttachments = attachments.data();
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = dependencies.data();
    check(vkCreateRenderPass(device, &info, nullptr, &target.pass), "vkCreateRenderPass(scene)");
}

SceneUniform VulkanRenderer::Impl::sceneUniform(const RenderSettings& settings) {
    SceneUniform uniform;
    uniform.viewProjection = settings.viewProjection;
    uniform.model = settings.model;
    auto light = settings.lightDirection;
    if (glm::dot(light, light) < 0.000001f) light = {0, -1, 0};
    uniform.lightDirection = glm::vec4(light, 0);
    uniform.parameters = {std::clamp(settings.ambient, 0.0f, 1.0f), settings.unlit ? 1.0f : 0.0f, 0, 0};
    if (aa.taa) {
        const bool perspective = perspectiveProjection(settings.viewProjection);
        if (temporalHistory.valid && (settings.unlit != temporalHistory.previous.unlit ||
            settings.ambient != temporalHistory.previous.ambient ||
            settings.lightDirection != temporalHistory.previous.lightDirection ||
            settings.background != temporalHistory.previous.background ||
            perspective != perspectiveProjection(temporalHistory.previous.viewProjection)))
            resetTemporalHistory();
        const auto halton = [](std::uint32_t index, std::uint32_t base) {
            float value = 0, fraction = 1;
            while (index) {
                fraction /= static_cast<float>(base);
                value += fraction * static_cast<float>(index % base);
                index /= base;
            }
            return value;
        };
        const glm::vec2 inverse{1.0f / static_cast<float>(viewport->width),
                                1.0f / static_cast<float>(viewport->height)};
        glm::mat4 jitter(1);
        jitter[3][0] = 2 * (halton(temporalHistory.jitterIndex + 1, 2) - 0.5f) * inverse.x;
        jitter[3][1] = 2 * (halton(temporalHistory.jitterIndex + 1, 3) - 0.5f) * inverse.y;
        // Clip translation works for an already combined VP, including ortho.
        uniform.viewProjection = jitter * settings.viewProjection;
        uniform.currentViewProjection = settings.viewProjection;
        uniform.previousViewProjection = temporalHistory.valid ? temporalHistory.previous.viewProjection : settings.viewProjection;
        uniform.previousModel = temporalHistory.valid ? temporalHistory.previous.model : settings.model;
        uniform.temporalInfo = {inverse.x, inverse.y, perspective ? 1.0f : 0.0f,
            (temporalHistory.valid ? perspectiveProjection(temporalHistory.previous.viewProjection) : perspective) ? 1.0f : 0.0f};
    }
    return uniform;
}

void VulkanRenderer::Impl::drawScene(const RenderSettings& settings) {
    const auto viewportWidth = viewport->width, viewportHeight = viewport->height;
    const auto& scene = scenePipeline(aa.msaaSamples, aa.taa);
    const auto uniform = sceneUniform(settings);
    std::memcpy(uniformBuffer.mapped, &uniform, sizeof(uniform));
    std::array<VkClearValue, 3> clears{};
    clears[0].color = {{settings.background.r, settings.background.g, settings.background.b, 1}};
    clears[1].depthStencil = {1, 0};
    clears[2].color = {{0, 0, -1, -1}};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = scene.pass;
    begin.framebuffer = viewport->sceneFrame;
    begin.renderArea = {{0, 0}, {viewportWidth, viewportHeight}};
    begin.clearValueCount = aa.taa ? 3u : 2u;
    begin.pClearValues = clears.data();
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewportArea{0, 0, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0, 1};
    VkRect2D scissor{{0, 0}, {viewportWidth, viewportHeight}};
    vkCmdSetViewport(command, 0, 1, &viewportArea);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, scene.pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &sceneDescriptor, 0,
                            nullptr);
    if (model)
        for (const auto& mesh : model->meshes) {
            const auto& material = model->materials[mesh.material];
            constexpr VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(command, 0, 1, &mesh.vertices.handle, &offset);
            vkCmdBindIndexBuffer(command, mesh.indices.handle, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1,
                                    &material.descriptor, 0, nullptr);
            vkCmdPushConstants(command, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MaterialPush),
                               &material.parameters);
            vkCmdDrawIndexed(command, mesh.indexCount, 1, 0, 0, 0);
        }
    vkCmdEndRenderPass(command);
    sceneRendered = true;
}
}
