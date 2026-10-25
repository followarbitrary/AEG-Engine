#include "VulkanRendererImpl.hpp"

namespace aeg::render {
void VulkanRenderer::Impl::captureViewport(const std::filesystem::path& path) {
    if (!sceneRendered) throw std::runtime_error("Cannot capture before the first rendered scene");
    const auto viewportWidth = viewport->width, viewportHeight = viewport->height;
    const auto& viewportColor = viewport->output();
    check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(capture)");
    const bool halfFloat = aa.taa && !aa.fxaa;
    const auto pixelBytes = halfFloat ? 8u : 4u;
    const auto bytes = static_cast<VkDeviceSize>(viewportWidth) * viewportHeight * pixelBytes;
    auto staging =
        resources.buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    resources.immediate([&](VkCommandBuffer cmd) {
        detail::barrier(cmd, viewportColor.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                        VK_ACCESS_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {viewportWidth, viewportHeight, 1};
        vkCmdCopyImageToBuffer(cmd, viewportColor.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.handle, 1,
                               &copy);
        detail::barrier(cmd, viewportColor.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = staging.handle;
        host.size = bytes;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                             &host, 0, nullptr);
    });
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot write scene capture: " + foundation::pathToUtf8(path));
    output << "P6\n" << viewportWidth << ' ' << viewportHeight << "\n255\n";
    const auto* pixels = static_cast<const unsigned char*>(staging.mapped);
    std::vector<unsigned char> rgb(static_cast<std::size_t>(viewportWidth) * viewportHeight * 3);
    for (std::size_t i = 0, j = 0; i < static_cast<std::size_t>(bytes); i += pixelBytes, j += 3)
        for (std::size_t c = 0; c < 3; ++c) {
            float linear = pixels[i + c] / 255.0f;
            if (halfFloat) {
                const auto half = std::uint16_t(pixels[i + c * 2]) |
                                  (std::uint16_t(pixels[i + c * 2 + 1]) << 8);
                const auto exponent = (half >> 10) & 31, fraction = half & 1023;
                linear = exponent == 31 ? (fraction ? 0.0f : 1.0f) :
                    std::ldexp(exponent ? 1.0f + float(fraction) / 1024 : float(fraction) / 1024,
                               exponent ? int(exponent) - 15 : -14);
                if (half & 0x8000) linear = -linear;
            }
            linear = std::clamp(linear, 0.0f, 1.0f);
            const float srgb =
                linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            rgb[j + c] = static_cast<unsigned char>(std::clamp(srgb * 255.0f, 0.0f, 255.0f) + 0.5f);
        }
    output.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (!output) throw std::runtime_error("Failed writing scene capture");
    std::cout << "Scene capture: " << foundation::pathToUtf8(path) << '\n';
}
}
