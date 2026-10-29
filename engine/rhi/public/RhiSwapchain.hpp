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
    // Load-compatible pass/framebuffer used for the final UI overlay.  The
    // default keeps lightweight/fake swapchains source-compatible; Vulkan
    // provides a distinct load-op render pass for ImGui pipelines.
    virtual RenderPass& loadRenderPass() { return renderPass(); }
    virtual Framebuffer& loadFramebuffer() { return framebuffer(); }
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
