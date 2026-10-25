#include "VulkanRendererImpl.hpp"

namespace aeg::render {
void VulkanRenderer::Impl::destroyViewport() noexcept {
    viewport.reset();
    sceneRendered = false;
}

std::unique_ptr<ViewportTargets> VulkanRenderer::Impl::createViewport(std::uint32_t width, std::uint32_t height,
                                               const AntialiasingSettings& settings) {
    auto& scene = scenePipeline(settings.msaaSamples, settings.taa);
    auto candidate = std::make_unique<ViewportTargets>();
    candidate->device = device;
    candidate->uiPool = uiPool;
    candidate->width = width;
    candidate->height = height;
    candidate->color = resources.image(width, height, sceneFormat,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                    VK_IMAGE_ASPECT_COLOR_BIT);
    candidate->depth = resources.image(width, height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                    VK_IMAGE_ASPECT_DEPTH_BIT, scene.samples);
    const bool multisampled = settings.msaaSamples > 1;
    if (multisampled)
        candidate->multisample = resources.image(width, height, sceneFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, scene.samples);
    if (settings.taa)
        candidate->motion = resources.image(width, height, motionFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
    const std::array attachments{multisampled ? candidate->multisample.view : candidate->color.view,
                                candidate->depth.view, settings.taa ? candidate->motion.view : candidate->color.view};
    VkFramebufferCreateInfo frame{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    frame.renderPass = scene.pass;
    frame.attachmentCount = (multisampled || settings.taa) ? 3u : 2u;
    frame.pAttachments = attachments.data();
    frame.width = width;
    frame.height = height;
    frame.layers = 1;
    check(vkCreateFramebuffer(device, &frame, nullptr, &candidate->sceneFrame), "vkCreateFramebuffer(viewport)");
    if (settings.fxaa || settings.taa) {
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            (settings.fxaa ? 1u : 0u) + (settings.taa ? 8u : 0u)};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = (settings.fxaa ? 1u : 0u) + (settings.taa ? 2u : 0u);
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device, &pool, nullptr, &candidate->postPool), "vkCreateDescriptorPool(post)");
    }
    if (settings.fxaa) {
        candidate->filtered = resources.image(width, height, sceneFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
        frame.renderPass = postPass;
        frame.attachmentCount = 1;
        frame.pAttachments = &candidate->filtered.view;
        check(vkCreateFramebuffer(device, &frame, nullptr, &candidate->postFrame), "vkCreateFramebuffer(FXAA)");
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = candidate->postPool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &materialLayout;
        check(vkAllocateDescriptorSets(device, &allocate, &candidate->postDescriptor), "vkAllocateDescriptorSets(FXAA)");
        VkDescriptorImageInfo image{viewportSampler, candidate->color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = candidate->postDescriptor;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    if (settings.taa) {
        for (std::size_t i = 0; i < 2; ++i) {
            const auto usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            candidate->historyColor[i] = resources.image(width, height, historyFormat, usage,
                                                        VK_IMAGE_ASPECT_COLOR_BIT);
            candidate->historyDepth[i] = resources.image(width, height, historyDepthFormat, usage,
                                                        VK_IMAGE_ASPECT_COLOR_BIT);
            const std::array views{candidate->historyColor[i].view, candidate->historyDepth[i].view};
            frame.renderPass = temporalPass;
            frame.attachmentCount = static_cast<std::uint32_t>(views.size());
            frame.pAttachments = views.data();
            check(vkCreateFramebuffer(device, &frame, nullptr, &candidate->temporalFrames[i]),
                  "vkCreateFramebuffer(TAA)");
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.descriptorPool = candidate->postPool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &temporalSetLayout;
            check(vkAllocateDescriptorSets(device, &allocate, &candidate->temporalDescriptors[i]),
                  "vkAllocateDescriptorSets(TAA)");
            const std::array<VkDescriptorImageInfo, 4> images{{
                {pointSampler, candidate->color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {pointSampler, candidate->motion.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {viewportSampler, candidate->historyColor[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {pointSampler, candidate->historyDepth[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
            std::array<VkWriteDescriptorSet, 4> writes{};
            for (std::uint32_t binding = 0; binding < writes.size(); ++binding) {
                auto& write = writes[binding];
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = candidate->temporalDescriptors[i];
                write.dstBinding = binding;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo = &images[binding];
            }
            vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
        // Even unused first-frame history descriptors must name images in
        // valid layouts. Contents are ignored until a submitted frame exists.
        resources.immediate([&](VkCommandBuffer cmd) {
            for (std::size_t i = 0; i < 2; ++i)
                for (const auto* image : {&candidate->historyColor[i], &candidate->historyDepth[i]})
                    detail::barrier(cmd, image->handle, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        });
    }
    candidate->descriptor = ImGui_ImplVulkan_AddTexture(viewportSampler, candidate->output().view,
                                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!candidate->descriptor) throw std::runtime_error("Cannot allocate ImGui viewport descriptor");
    return candidate;
}

void VulkanRenderer::Impl::resizeViewport(std::uint32_t width, std::uint32_t height) {
    const auto maximum = aa.taa ? maximumTemporalViewport : maximumViewport;
    width = std::clamp(width, 1u, maximum);
    height = std::clamp(height, 1u, maximum);
    if (viewport && width == viewport->width && height == viewport->height) return;
    check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(viewport)");
    auto replacement = createViewport(width, height, aa);
    viewport.swap(replacement);
    sceneRendered = false;
    resetTemporalHistory();
}
}
