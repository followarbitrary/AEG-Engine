#include "VulkanBackendInternal.hpp"

namespace aeg::rhi::vulkan::detail {

void VulkanCommands::initializeTimestampQueries() {
    if (timestampPool || !owner->timestampValidBits || owner->properties.limits.timestampPeriod <= 0.0f)
        return;
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = maxGpuPassQueries;
    // Timestamp queries are optional instrumentation. A device that cannot
    // allocate the pool still renders normally; the UI simply reports no data.
    if (vkCreateQueryPool(owner->handle, &info, nullptr, &timestampPool) != VK_SUCCESS) timestampPool = VK_NULL_HANDLE;
}

void VulkanCommands::destroyTimestampQueries() noexcept {
    if (timestampPool && owner->handle) vkDestroyQueryPool(owner->handle, timestampPool, nullptr);
    timestampPool = VK_NULL_HANDLE;
    pendingGpuPasses.clear();
    activeGpuPasses.clear();
    completedGpuPasses.clear();
    nextTimestampQuery = 0;
}

void VulkanCommands::beginTimestampFrame() {
    pendingGpuPasses.clear();
    activeGpuPasses.clear();
    nextTimestampQuery = 0;
    if (timestampPool) vkCmdResetQueryPool(handle, timestampPool, 0, maxGpuPassQueries);
}

void VulkanCommands::resolveTimestampQueries() {
    completedGpuPasses.clear();
    if (!timestampPool) return;
    completedGpuPasses.reserve(pendingGpuPasses.size());
    for (const auto& pending : pendingGpuPasses) {
        GpuPassTiming timing;
        timing.name = pending.name;
        if (pending.beginQuery != ~std::uint32_t{0} && pending.endQuery != ~std::uint32_t{0}) {
            std::array<std::uint64_t, 2> values{};
            const auto result = vkGetQueryPoolResults(owner->handle, timestampPool, pending.beginQuery, 2,
                                                      sizeof(values), values.data(), sizeof(std::uint64_t),
                                                      VK_QUERY_RESULT_64_BIT);
            if (result == VK_SUCCESS) {
                std::uint64_t delta = values[1] - values[0];
                if (owner->timestampValidBits < 64)
                    delta &= (std::uint64_t{1} << owner->timestampValidBits) - 1;
                timing.milliseconds = static_cast<double>(delta) * owner->properties.limits.timestampPeriod * 1.0e-6;
                timing.valid = true;
            }
        }
        completedGpuPasses.push_back(std::move(timing));
    }
}

void VulkanCommands::beginDebugLabel(const std::string_view name) {
    if (!owner->cmdBeginDebugUtilsLabel || name.empty()) return;
    VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    std::string copy(name);
    label.pLabelName = copy.c_str();
    label.color[0] = 0.18f;
    label.color[1] = 0.50f;
    label.color[2] = 0.85f;
    label.color[3] = 1.0f;
    owner->cmdBeginDebugUtilsLabel(handle, &label);
}

void VulkanCommands::endDebugLabel() {
    if (owner->cmdEndDebugUtilsLabel) owner->cmdEndDebugUtilsLabel(handle);
}

void VulkanCommands::beginGpuPass(const std::string_view name) {
    if (!timestampPool) return;
    PendingGpuPass pending;
    pending.name = name;
    const auto pendingIndex = static_cast<std::uint32_t>(pendingGpuPasses.size());
    if (nextTimestampQuery + 1 < maxGpuPassQueries) {
        pending.beginQuery = nextTimestampQuery++;
        pending.endQuery = nextTimestampQuery++;
        vkCmdWriteTimestamp(handle, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestampPool, pending.beginQuery);
    }
    pendingGpuPasses.push_back(std::move(pending));
    activeGpuPasses.push_back(pendingIndex);
}

void VulkanCommands::endGpuPass() {
    if (!timestampPool || activeGpuPasses.empty()) return;
    const auto pendingIndex = activeGpuPasses.back();
    activeGpuPasses.pop_back();
    auto& pending = pendingGpuPasses[pendingIndex];
    if (pending.endQuery != ~std::uint32_t{0})
        vkCmdWriteTimestamp(handle, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestampPool, pending.endQuery);
}

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
    case ResourceState::Storage:
        return VK_IMAGE_LAYOUT_GENERAL;
    }
    throw std::invalid_argument("Unknown RHI resource state");
}
StateScope stateScope(ResourceState state) {
    switch (state) {
    case ResourceState::Undefined:
        return {0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
    case ResourceState::Sampled:
        return {VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT};
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
    case ResourceState::Storage:
        return {VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT};
    case ResourceState::Indirect:
        return {VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT};
    }
    throw std::invalid_argument("Unknown RHI resource state");
}
void VulkanDevice::immediate(const std::function<void(VulkanCommands&)>& record) {
    if (!record) throw std::invalid_argument("Missing immediate recording callback");
    VulkanCommands commands(*this);
    VkFence fence{};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(handle, &fenceInfo, nullptr, &fence), "vkCreateFence(immediate)");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    try {
        check(vkAllocateCommandBuffers(handle, &allocate, &commands.handle), "vkAllocateCommandBuffers(immediate)");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(commands.handle, &begin), "vkBeginCommandBuffer(immediate)");
        record(commands);
        if (commands.inPass) throw std::logic_error("Immediate render pass was not ended");
        check(vkEndCommandBuffer(commands.handle), "vkEndCommandBuffer(immediate)");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands.handle;
        check(vkQueueSubmit(graphicsQueue, 1, &submit, fence), "vkQueueSubmit(immediate)");
        const auto firstWait = vkWaitForFences(handle, 1, &fence, VK_TRUE, UINT64_MAX);
        auto completion = firstWait;
        // A recoverable host-side wait failure cannot let the caller destroy resources
        // still referenced by this submission. Drain before propagating that failure.
        while (completion != VK_SUCCESS && completion != VK_ERROR_DEVICE_LOST) {
            completion = vkDeviceWaitIdle(handle);
            if (completion != VK_SUCCESS && completion != VK_ERROR_DEVICE_LOST)
                completion = vkWaitForFences(handle, 1, &fence, VK_TRUE, UINT64_MAX);
        }
        check(firstWait, "vkWaitForFences(immediate)");
    } catch (...) {
        if (commands.handle) vkFreeCommandBuffers(handle, pool, 1, &commands.handle);
        vkDestroyFence(handle, fence, nullptr);
        throw;
    }
    vkFreeCommandBuffers(handle, pool, 1, &commands.handle);
    vkDestroyFence(handle, fence, nullptr);
}
void VulkanDevice::submitImmediate(const std::function<void(CommandList&)>& record) {
    if (!record) throw std::invalid_argument("Missing immediate recording callback");
    immediate([&](VulkanCommands& commands) { record(commands); });
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
    if (!inPass) throw std::logic_error("Graphics pipeline outside render pass");
    vkCmdBindPipeline(handle, VK_PIPELINE_BIND_POINT_GRAPHICS, owned<VulkanPipeline>(*owner, pipeline).handle);
}
void VulkanCommands::bindComputePipeline(ComputePipeline& generic) {
    if (inPass || !owner->caps.compute)
        throw std::logic_error("Compute pipeline requires compute outside a render pass");
    auto& pipeline = owned<VulkanComputePipeline>(*owner, generic);
    if (computeBoundLayout != pipeline.layout) computeSets.clear();
    computeBoundLayout = pipeline.layout;
    computePipeline = &pipeline;
    vkCmdBindPipeline(handle, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle);
}
void VulkanCommands::bindSet(PipelineLayout& genericLayout, std::uint32_t index, BindingSet& genericSet,
                             PipelineBindPoint point) {
    if ((point == PipelineBindPoint::Compute && (inPass || !owner->caps.compute)) ||
        (point == PipelineBindPoint::Graphics && !inPass) ||
        (point != PipelineBindPoint::Graphics && point != PipelineBindPoint::Compute))
        throw std::invalid_argument("Invalid descriptor bind point or render pass state");
    auto& layout = owned<VulkanPipelineLayout>(*owner, genericLayout);
    auto& set = owned<VulkanBindingSet>(*owner, genericSet);
    if (index >= layout.desc.sets.size()) throw std::invalid_argument("Binding set index exceeds pipeline layout");
    auto& expected = owned<VulkanBindingLayout>(*owner, *layout.desc.sets[index]);
    if (expected.desc.bindings.size() != set.desc.bindings.size())
        throw std::invalid_argument("Binding set layout mismatch");
    for (const auto& binding : expected.desc.bindings)
        if (!std::ranges::any_of(set.desc.bindings, [&](const auto& other) {
                return binding.binding == other.binding && binding.type == other.type &&
                       binding.stages == other.stages && binding.arraySize == other.arraySize;
            }))
            throw std::invalid_argument("Binding set layout mismatch");
    if (point == PipelineBindPoint::Compute) {
        if (computePipeline && computePipeline->layout != &layout)
            throw std::invalid_argument("Compute pipeline layout mismatch");
        if (computeBoundLayout != &layout) computeSets.clear();
        computeBoundLayout = &layout;
        if (computeSets.size() <= index) computeSets.resize(index + 1);
        computeSets[index] = &set;
    }
    vkCmdBindDescriptorSets(
        handle, point == PipelineBindPoint::Graphics ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE,
        layout.handle, index, 1, &set.handle, 0, nullptr);
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
    indexBound = true;
}
void VulkanCommands::pushConstants(PipelineLayout& generic, std::span<const std::byte> bytes) {
    auto& layout = owned<VulkanPipelineLayout>(*owner, generic);
    if (bytes.empty() || bytes.size() > layout.desc.pushConstantBytes || bytes.size() % 4)
        throw std::invalid_argument("Invalid push constant byte count");
    if (contains(layout.desc.pushConstantStages, ShaderStage::Compute) &&
        (inPass || !owner->caps.compute || (computePipeline && computePipeline->layout != &layout)))
        throw std::invalid_argument("Invalid compute push constant state or layout");
    vkCmdPushConstants(handle, layout.handle, shaderStages(layout.desc.pushConstantStages), 0,
                       static_cast<std::uint32_t>(bytes.size()), bytes.data());
}
void VulkanCommands::draw(std::uint32_t vertices) {
    if (!inPass) throw std::logic_error("Draw outside render pass");
    vkCmdDraw(handle, vertices, 1, 0, 0);
}
void VulkanCommands::drawIndexed(std::uint32_t indices) {
    if (!inPass || !indexBound) throw std::logic_error("Indexed draw requires a render pass and index buffer");
    vkCmdDrawIndexed(handle, indices, 1, 0, 0, 0);
}
void VulkanCommands::drawIndexedIndirect(Buffer& generic, const std::size_t offset, const std::uint32_t drawCount,
                                         const std::uint32_t stride) {
    if (!inPass || !indexBound || !owner->caps.indirectDraw || !drawCount ||
        drawCount > owner->properties.limits.maxDrawIndirectCount || stride < sizeof(std::uint32_t) * 5 ||
        stride % 4 || offset % 4)
        throw std::invalid_argument("Indirect indexed draw requires a render pass and valid arguments");
    auto& buffer = owned<VulkanBuffer>(*owner, generic);
    if (!contains(buffer.desc.usage, BufferUsage::Indirect) || offset > buffer.desc.size ||
        drawCount > (buffer.desc.size - offset) / stride)
        throw std::invalid_argument("Buffer lacks indirect usage or is too small");
    vkCmdDrawIndexedIndirect(handle, buffer.handle, offset, drawCount, stride);
}
void VulkanCommands::dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    if (inPass || !owner->caps.compute || !computePipeline || !x || !y || !z)
        throw std::invalid_argument("Dispatch requires a compute pipeline outside a render pass and nonzero groups");
    const std::array groups{x, y, z};
    for (std::size_t i = 0; i < groups.size(); ++i)
        if (groups[i] > owner->caps.maximumComputeGroupCount[i])
            throw std::invalid_argument("Dispatch group count exceeds device limit");
    const auto setCount = computePipeline->layout->desc.sets.size();
    if (computeBoundLayout != computePipeline->layout || computeSets.size() < setCount)
        throw std::invalid_argument("Compute pipeline descriptor sets are not bound");
    for (std::size_t i = 0; i < setCount; ++i)
        if (!computeSets[i]) {
            throw std::invalid_argument("Compute descriptor set is not bound");
        } else {
            for (const auto& binding : computeSets[i]->desc.bindings)
                if (contains(binding.stages, ShaderStage::Compute))
                    for (std::uint32_t element = 0; element < binding.arraySize; ++element)
                        if (!computeSets[i]->initializedBindings.contains({binding.binding, element}))
                            throw std::invalid_argument("Compute descriptor binding is not initialized");
        }
    vkCmdDispatch(handle, x, y, z);
}
void VulkanCommands::transition(Texture& generic, ResourceState before, ResourceState after) {
    if (inPass || after == ResourceState::Undefined) throw std::invalid_argument("Invalid image transition");
    auto& texture = owned<VulkanTexture>(*owner, generic);
    if ((before == ResourceState::Storage || after == ResourceState::Storage) &&
        (!owner->caps.compute || !contains(texture.desc.usage, TextureUsage::Storage)))
        throw std::invalid_argument("Storage transition requires a compute-capable storage texture");
    auto source = stateScope(before), destination = stateScope(after);
    if (!owner->caps.compute) {
        source.stages &= ~VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        destination.stages &= ~VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    if (before == after && before != ResourceState::Storage) return;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = source.access;
    barrier.dstAccessMask = destination.access;
    barrier.oldLayout = imageLayout(before);
    barrier.newLayout = imageLayout(after);
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.handle;
    barrier.subresourceRange = {imageAspect(texture.desc.format), 0, texture.desc.mipLevels, 0,
                                textureLayerCount(texture.desc)};
    vkCmdPipelineBarrier(handle, source.stages, destination.stages, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
void VulkanCommands::transition(Buffer& generic, ResourceState before, ResourceState after) {
    if (inPass || after == ResourceState::Undefined)
        throw std::invalid_argument("Invalid buffer transition");
    const auto valid = [](ResourceState state) {
        return state == ResourceState::Undefined || state == ResourceState::Storage ||
               state == ResourceState::Indirect || state == ResourceState::CopySource;
    };
    if (!valid(before) || !valid(after)) throw std::invalid_argument("Buffer transition requires a buffer state");
    auto& buffer = owned<VulkanBuffer>(*owner, generic);
    if ((before == ResourceState::Storage || after == ResourceState::Storage) &&
        !contains(buffer.desc.usage, BufferUsage::Storage))
        throw std::invalid_argument("Storage transition requires a storage buffer");
    if (after == ResourceState::Indirect && !contains(buffer.desc.usage, BufferUsage::Indirect))
        throw std::invalid_argument("Indirect transition requires indirect usage");
    const auto source = stateScope(before), destination = stateScope(after);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = source.access;
    barrier.dstAccessMask = destination.access;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer.handle;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(handle, source.stages, destination.stages, 0, 0, nullptr, 1, &barrier, 0, nullptr);
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
void VulkanCommands::copyTextureToBuffer(Texture& genericSource, Buffer& genericDestination,
                                         TextureSubresource subresource) {
    if (inPass) throw std::logic_error("Copy inside render pass");
    auto& source = owned<VulkanTexture>(*owner, genericSource);
    auto& destination = owned<VulkanBuffer>(*owner, genericDestination);
    const auto extent = mipExtent(source.desc, subresource.mipLevel);
    const auto pixelCount = static_cast<std::uint64_t>(extent.width) * extent.height;
    if (pixelCount > std::numeric_limits<std::uint64_t>::max() / formatByteSize(source.desc.format))
        throw std::invalid_argument("Texture copy size overflows");
    const auto bytes = pixelCount * formatByteSize(source.desc.format);
    if (isDepthFormat(source.desc.format) || source.desc.samples != 1 || bytes > destination.desc.size ||
        !contains(source.desc.usage, TextureUsage::CopySource) ||
        !contains(destination.desc.usage, BufferUsage::CopyDestination) ||
        subresource.layer >= textureLayerCount(source.desc))
        throw std::invalid_argument("Invalid texture readback copy");
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, subresource.mipLevel, subresource.layer, 1};
    copy.imageExtent = {extent.width, extent.height, 1};
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
