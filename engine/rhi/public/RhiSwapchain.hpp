#pragma once
#include <RhiDevice.hpp>
namespace aeg::rhi {
class Swapchain {
public:
    virtual ~Swapchain() = default;
    // Wait/acquire/begin recording; false means no frame and no UI callback.
    virtual bool beginFrame() = 0;
    virtual CommandList& commandList() = 0;
    virtual RenderPass& renderPass() = 0;
    virtual Framebuffer& framebuffer() = 0;
    [[nodiscard]] virtual Extent2D extent() const = 0;
    [[nodiscard]] virtual std::uint32_t minimumImageCount() const = 0;
    [[nodiscard]] virtual std::uint32_t imageCount() const = 0;
    [[nodiscard]] virtual Texture& image() = 0;
    [[nodiscard]] virtual ResourceState imageState() const = 0;
    // A successful submit advances temporal state even if present requires recreation.
    virtual void submit() = 0;
    virtual void present() = 0;
};
}
