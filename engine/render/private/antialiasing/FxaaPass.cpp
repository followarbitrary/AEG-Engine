#include "VulkanRendererImpl.hpp"

namespace aeg::render {
void VulkanRenderer::Impl::drawFxaa() {
    if (!aa.fxaa) return;
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = postPass;
    pass.framebuffer = viewport->postFrame;
    pass.renderArea = {{0, 0}, {viewport->width, viewport->height}};
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport target{0, 0, static_cast<float>(viewport->width), static_cast<float>(viewport->height), 0, 1};
    VkRect2D scissor{{0, 0}, {viewport->width, viewport->height}};
    vkCmdSetViewport(command, 0, 1, &target);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout, 0, 1,
                            &viewport->postDescriptor, 0, nullptr);
    const glm::vec2 reciprocal{1.0f / static_cast<float>(viewport->width),
                               1.0f / static_cast<float>(viewport->height)};
    vkCmdPushConstants(command, postLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(reciprocal), &reciprocal);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdEndRenderPass(command);
}
}
