#include "VulkanRendererImpl.hpp"

namespace aeg::render {
void VulkanRenderer::Impl::resetTemporalHistory() noexcept {
    temporalHistory.valid = false;
    temporalHistory.frames = 0;
    temporalHistory.readIndex = 1;
    temporalHistory.jitterIndex = 0;
}

bool VulkanRenderer::Impl::perspectiveProjection(const glm::mat4& matrix) {
    return glm::dot(glm::vec3(matrix[0][3], matrix[1][3], matrix[2][3]),
                    glm::vec3(matrix[0][3], matrix[1][3], matrix[2][3])) > 1e-12f;
}

void VulkanRenderer::Impl::drawTemporal() {
    if (!aa.taa) return;
    const auto writeIndex = 1u - temporalHistory.readIndex;
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = temporalPass;
    pass.framebuffer = viewport->temporalFrames[writeIndex];
    pass.renderArea = {{0, 0}, {viewport->width, viewport->height}};
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport target{0, 0, static_cast<float>(viewport->width), static_cast<float>(viewport->height), 0, 1};
    VkRect2D scissor{{0, 0}, {viewport->width, viewport->height}};
    vkCmdSetViewport(command, 0, 1, &target);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, temporalPipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, temporalLayout, 0, 1,
                            &viewport->temporalDescriptors[temporalHistory.readIndex], 0, nullptr);
    const TemporalPush push{{1.0f / static_cast<float>(viewport->width),
                              1.0f / static_cast<float>(viewport->height)},
        std::min(0.9f, static_cast<float>(temporalHistory.frames) / (static_cast<float>(temporalHistory.frames) + 1)),
        temporalHistory.valid ? 1u : 0u};
    vkCmdPushConstants(command, temporalLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdEndRenderPass(command);
}

void VulkanRenderer::Impl::pointFinalDescriptorAtTemporalOutput() {
    if (!aa.taa) return;
    // Image has recorded only this stable descriptor handle. The previous
    // frame fence is complete; updating its view here cannot invalidate it.
    const VkDescriptorImageInfo image{viewportSampler, viewport->historyColor[1u - temporalHistory.readIndex].view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = aa.fxaa ? viewport->postDescriptor : viewport->descriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}
}
