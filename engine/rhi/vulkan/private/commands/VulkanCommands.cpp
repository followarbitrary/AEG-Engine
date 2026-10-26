#include "VulkanBackendInternal.hpp"

namespace aeg::rhi::vulkan::detail {
VkImageLayout imageLayout(ResourceState state) {
    switch (state) {
    case ResourceState::Undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case ResourceState::Sampled:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ResourceState::ColorAttachment:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthAttachment:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ResourceState::CopySource:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ResourceState::CopyDestination:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case ResourceState::Present:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    throw std::invalid_argument("Unknown RHI resource state");
}
StateScope stateScope(ResourceState state) {
    switch (state) {
    case ResourceState::Undefined:
        return {0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
    case ResourceState::Sampled:
        return {VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
    case ResourceState::ColorAttachment:
        return {VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    case ResourceState::DepthAttachment:
        return {VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT};
    case ResourceState::CopySource:
        return {VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
    case ResourceState::CopyDestination:
        return {VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
    case ResourceState::Present:
        return {0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT};
    }
    throw std::invalid_argument("Unknown RHI resource state");
}
void VulkanDevice::immediate(const std::function<void(VulkanCommands&)>& record) {
    VulkanCommands commands(*this);
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(handle, &allocate, &commands.handle), "vkAllocateCommandBuffers(immediate)");
    try {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(commands.handle, &begin), "vkBeginCommandBuffer(immediate)");
        record(commands);
        if (commands.inPass) throw std::logic_error("Immediate render pass was not ended");
        check(vkEndCommandBuffer(commands.handle), "vkEndCommandBuffer(immediate)");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands.handle;
        check(vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit(immediate)");
        check(vkQueueWaitIdle(graphicsQueue), "vkQueueWaitIdle(immediate)");
    } catch (...) {
        vkFreeCommandBuffers(handle, pool, 1, &commands.handle);
        throw;
    }
    vkFreeCommandBuffers(handle, pool, 1, &commands.handle);
}
void VulkanCommands::beginRenderPass(RenderPass& genericPass, Framebuffer& genericFrame, Extent2D extent,
                                     std::span<const ClearValue> clears) {
    if (inPass) throw std::logic_error("A render pass is already active");
    auto& pass = owned<VulkanRenderPass>(*owner, genericPass);
    auto& frame = owned<VulkanFramebuffer>(*owner, genericFrame);
    if (frame.pass != &pass || !extent.width || !extent.height || extent.width > frame.size.width ||
        extent.height > frame.size.height || clears.size() > pass.desc.attachments.size())
        throw std::invalid_argument("Render pass/framebuffer mismatch");
    for (std::size_t i = 0; i < pass.desc.attachments.size(); ++i)
        if (pass.desc.attachments[i].load == LoadOp::Clear && i >= clears.size())
            throw std::invalid_argument("Missing attachment clear value");
    std::vector<VkClearValue> values(clears.size());
    for (std::size_t i = 0; i < clears.size(); ++i) {
        if (isDepthFormat(pass.desc.attachments[i].format))
            values[i].depthStencil = {clears[i].depth, clears[i].stencil};
        else
            std::copy(clears[i].color.begin(), clears[i].color.end(), values[i].color.float32);
    }
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = pass.handle;
    begin.framebuffer = frame.handle;
    begin.renderArea = {{0, 0}, {extent.width, extent.height}};
    begin.clearValueCount = static_cast<std::uint32_t>(values.size());
    begin.pClearValues = values.data();
    vkCmdBeginRenderPass(handle, &begin, VK_SUBPASS_CONTENTS_INLINE);
    inPass = true;
}
void VulkanCommands::endRenderPass() {
    if (!inPass) throw std::logic_error("No active render pass");
    vkCmdEndRenderPass(handle);
    inPass = false;
}
void VulkanCommands::setViewport(Extent2D extent) {
    if (!extent.width || !extent.height) throw std::invalid_argument("Viewport must be nonempty");
    VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, {extent.width, extent.height}};
    vkCmdSetViewport(handle, 0, 1, &viewport);
    vkCmdSetScissor(handle, 0, 1, &scissor);
}
void VulkanCommands::bindPipeline(GraphicsPipeline& pipeline) {
    vkCmdBindPipeline(handle, VK_PIPELINE_BIND_POINT_GRAPHICS, owned<VulkanPipeline>(*owner, pipeline).handle);
}
void VulkanCommands::bindSet(PipelineLayout& genericLayout, std::uint32_t index, BindingSet& genericSet) {
    auto& layout = owned<VulkanPipelineLayout>(*owner, genericLayout);
    auto& set = owned<VulkanBindingSet>(*owner, genericSet);
    if (index >= layout.desc.sets.size()) throw std::invalid_argument("Binding set index exceeds pipeline layout");
    auto& expected = owned<VulkanBindingLayout>(*owner, *layout.desc.sets[index]);
    if (expected.desc.bindings.size() != set.desc.bindings.size())
        throw std::invalid_argument("Binding set layout mismatch");
    for (const auto& binding : expected.desc.bindings)
        if (!std::ranges::any_of(set.desc.bindings, [&](const auto& other) {
                return binding.binding == other.binding && binding.type == other.type && binding.stages == other.stages;
            }))
            throw std::invalid_argument("Binding set layout mismatch");
    vkCmdBindDescriptorSets(handle, VK_PIPELINE_BIND_POINT_GRAPHICS, layout.handle, index, 1, &set.handle, 0, nullptr);
}
void VulkanCommands::bindVertexBuffer(Buffer& generic) {
    auto& buffer = owned<VulkanBuffer>(*owner, generic);
    if (!contains(buffer.desc.usage, BufferUsage::Vertex)) throw std::invalid_argument("Buffer lacks vertex usage");
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(handle, 0, 1, &buffer.handle, &offset);
}
void VulkanCommands::bindIndexBuffer(Buffer& generic) {
    auto& buffer = owned<VulkanBuffer>(*owner, generic);
    if (!contains(buffer.desc.usage, BufferUsage::Index)) throw std::invalid_argument("Buffer lacks index usage");
    vkCmdBindIndexBuffer(handle, buffer.handle, 0, VK_INDEX_TYPE_UINT32);
}
void VulkanCommands::pushConstants(PipelineLayout& generic, std::span<const std::byte> bytes) {
    auto& layout = owned<VulkanPipelineLayout>(*owner, generic);
    if (bytes.empty() || bytes.size() > layout.desc.pushConstantBytes || bytes.size() % 4)
        throw std::invalid_argument("Invalid push constant byte count");
    vkCmdPushConstants(handle, layout.handle, shaderStages(layout.desc.pushConstantStages), 0,
                       static_cast<std::uint32_t>(bytes.size()), bytes.data());
}
void VulkanCommands::draw(std::uint32_t vertices) {
    if (!inPass) throw std::logic_error("Draw outside render pass");
    vkCmdDraw(handle, vertices, 1, 0, 0);
}
void VulkanCommands::drawIndexed(std::uint32_t indices) {
    if (!inPass) throw std::logic_error("Draw outside render pass");
    vkCmdDrawIndexed(handle, indices, 1, 0, 0, 0);
}
void VulkanCommands::transition(Texture& generic, ResourceState before, ResourceState after) {
    if (inPass || after == ResourceState::Undefined) throw std::invalid_argument("Invalid image transition");
    auto& texture = owned<VulkanTexture>(*owner, generic);
    const auto source = stateScope(before), destination = stateScope(after);
    if (before == after) return;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = source.access;
    barrier.dstAccessMask = destination.access;
    barrier.oldLayout = imageLayout(before);
    barrier.newLayout = imageLayout(after);
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.handle;
    barrier.subresourceRange = {imageAspect(texture.desc.format), 0, 1, 0, 1};
    vkCmdPipelineBarrier(handle, source.stages, destination.stages, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
void VulkanCommands::copyBuffer(Buffer& genericSource, Buffer& genericDestination, std::size_t bytes) {
    if (inPass) throw std::logic_error("Copy inside render pass");
    auto& source = owned<VulkanBuffer>(*owner, genericSource);
    auto& destination = owned<VulkanBuffer>(*owner, genericDestination);
    if (!bytes || bytes > source.desc.size || bytes > destination.desc.size ||
        !contains(source.desc.usage, BufferUsage::CopySource) ||
        !contains(destination.desc.usage, BufferUsage::CopyDestination) || &source == &destination)
        throw std::invalid_argument("Invalid buffer copy");
    VkBufferCopy copy{0, 0, bytes};
    vkCmdCopyBuffer(handle, source.handle, destination.handle, 1, &copy);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = destination.handle;
    barrier.size = bytes;
    vkCmdPipelineBarrier(handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1,
                         &barrier, 0, nullptr);
}
void VulkanCommands::copyTextureToBuffer(Texture& genericSource, Buffer& genericDestination) {
    if (inPass) throw std::logic_error("Copy inside render pass");
    auto& source = owned<VulkanTexture>(*owner, genericSource);
    auto& destination = owned<VulkanBuffer>(*owner, genericDestination);
    auto bytes = static_cast<std::uint64_t>(source.desc.extent.width) * source.desc.extent.height *
                 formatByteSize(source.desc.format);
    if (isDepthFormat(source.desc.format) || source.desc.samples != 1 || bytes > destination.desc.size ||
        !contains(source.desc.usage, TextureUsage::CopySource) ||
        !contains(destination.desc.usage, BufferUsage::CopyDestination))
        throw std::invalid_argument("Invalid texture readback copy");
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {source.desc.extent.width, source.desc.extent.height, 1};
    vkCmdCopyImageToBuffer(handle, source.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination.handle, 1, &copy);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = destination.handle;
    barrier.size = bytes;
    vkCmdPipelineBarrier(handle, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &barrier, 0,
                         nullptr);
}
VkCommandBuffer nativeCommandBuffer(Device& device, CommandList& commands) {
    return owned<VulkanCommands>(nativeDevice(device), commands).handle;
}
}
