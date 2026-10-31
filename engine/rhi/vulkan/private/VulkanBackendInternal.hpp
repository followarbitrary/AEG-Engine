#pragma once
#include "VmaConfig.hpp"
#include <windows.h>
#include "VulkanInterop.hpp"
#include <vk_mem_alloc.h>
#include <VulkanBackend.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>

namespace aeg::rhi::vulkan::detail {
inline void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (" + std::to_string(result) + ")");
}
VkFormat nativeFormat(Format format);
Format genericFormat(VkFormat format);
VkImageUsageFlags imageUsage(TextureUsage usage);
VkBufferUsageFlags bufferUsage(BufferUsage usage);
VkShaderStageFlags shaderStages(ShaderStage stages);
VkImageLayout imageLayout(ResourceState state);
VkImageAspectFlags imageAspect(Format format);
struct StateScope {
    VkAccessFlags access;
    VkPipelineStageFlags stages;
};
StateScope stateScope(ResourceState state);
class VulkanDevice;
template <class Native, class Generic> Native& owned(VulkanDevice& device, Generic& generic) {
    auto* native = dynamic_cast<Native*>(&generic);
    if (!native || native->owner != &device)
        throw std::invalid_argument("RHI resource belongs to a different backend or device");
    return *native;
}
class VulkanBuffer final : public Buffer {
public:
    VulkanDevice* owner;
    BufferDesc desc;
    VkBuffer handle{};
    VmaAllocation allocation{};
    void* mapping{};
    VulkanBuffer(VulkanDevice& device, BufferDesc description) : owner(&device), desc(description) {}
    ~VulkanBuffer() override;
    const BufferDesc& description() const override { return desc; }
    std::span<std::byte> mapped() override {
        return mapping ? std::span(static_cast<std::byte*>(mapping), desc.size) : std::span<std::byte>{};
    }
};
class VulkanTexture final : public Texture {
public:
    VulkanDevice* owner;
    TextureDesc desc;
    VkImage handle{};
    VmaAllocation allocation{};
    VkImageView view{};
    std::vector<VkImageView> storageViews;
    bool nonOwning{};
    VulkanTexture(VulkanDevice& device, TextureDesc description) : owner(&device), desc(description) {}
    ~VulkanTexture() override;
    const TextureDesc& description() const override { return desc; }
};
class VulkanSampler final : public Sampler {
public:
    VulkanDevice* owner;
    VkSampler handle{};
    explicit VulkanSampler(VulkanDevice& device) : owner(&device) {}
    ~VulkanSampler() override;
};
class VulkanShader final : public Shader {
public:
    VulkanDevice* owner;
    ShaderStage stage;
    VkShaderModule handle{};
    VulkanShader(VulkanDevice& device, ShaderStage value) : owner(&device), stage(value) {}
    ~VulkanShader() override;
};
class VulkanBindingLayout final : public BindingLayout {
public:
    VulkanDevice* owner;
    BindingLayoutDesc desc;
    VkDescriptorSetLayout handle{};
    VulkanBindingLayout(VulkanDevice& device, BindingLayoutDesc description)
        : owner(&device), desc(std::move(description)) {}
    ~VulkanBindingLayout() override;
};
class VulkanBindingSet final : public BindingSet {
public:
    VulkanDevice* owner;
    BindingLayoutDesc desc;
    VkDescriptorPool pool{};
    VkDescriptorSet handle{};
    std::set<std::pair<std::uint32_t, std::uint32_t>> initializedBindings;
    VulkanBindingSet(VulkanDevice& device, BindingLayoutDesc description)
        : owner(&device), desc(std::move(description)) {}
    ~VulkanBindingSet() override;
    void update(std::span<const BindingWrite> writes) override;
};
class VulkanPipelineLayout final : public PipelineLayout {
public:
    VulkanDevice* owner;
    PipelineLayoutDesc desc;
    VkPipelineLayout handle{};
    VulkanPipelineLayout(VulkanDevice& device, PipelineLayoutDesc description)
        : owner(&device), desc(std::move(description)) {}
    ~VulkanPipelineLayout() override;
};
class VulkanRenderPass final : public RenderPass {
public:
    VulkanDevice* owner;
    RenderPassDesc desc;
    VkRenderPass handle{};
    VulkanRenderPass(VulkanDevice& device, RenderPassDesc description) : owner(&device), desc(std::move(description)) {}
    ~VulkanRenderPass() override;
};
class VulkanFramebuffer final : public Framebuffer {
public:
    VulkanDevice* owner;
    VkFramebuffer handle{};
    VulkanRenderPass* pass{};
    Extent2D size{};
    explicit VulkanFramebuffer(VulkanDevice& device) : owner(&device) {}
    ~VulkanFramebuffer() override;
};
class VulkanPipeline final : public GraphicsPipeline {
public:
    VulkanDevice* owner;
    VkPipeline handle{};
    explicit VulkanPipeline(VulkanDevice& device) : owner(&device) {}
    ~VulkanPipeline() override;
};
class VulkanComputePipeline final : public ComputePipeline {
public:
    VulkanDevice* owner;
    VulkanPipelineLayout* layout{};
    VkPipeline handle{};
    explicit VulkanComputePipeline(VulkanDevice& device) : owner(&device) {}
    ~VulkanComputePipeline() override;
};
class VulkanCommands final : public CommandList {
public:
    VulkanDevice* owner;
    VkCommandBuffer handle{};
    bool inPass{};
    bool indexBound{};
    VulkanComputePipeline* computePipeline{};
    VulkanPipelineLayout* computeBoundLayout{};
    std::vector<VulkanBindingSet*> computeSets;
    explicit VulkanCommands(VulkanDevice& device) : owner(&device) {}
    void beginRenderPass(RenderPass&, Framebuffer&, Extent2D, std::span<const ClearValue>) override;
    void endRenderPass() override;
    void setViewport(Extent2D) override;
    void bindPipeline(GraphicsPipeline&) override;
    void bindComputePipeline(ComputePipeline&) override;
    void bindSet(PipelineLayout&, std::uint32_t, BindingSet&, PipelineBindPoint = PipelineBindPoint::Graphics) override;
    void bindVertexBuffer(Buffer&) override;
    void bindIndexBuffer(Buffer&) override;
    void pushConstants(PipelineLayout&, std::span<const std::byte>) override;
    void draw(std::uint32_t) override;
    void drawIndexed(std::uint32_t) override;
    void drawIndexedIndirect(Buffer&, std::size_t, std::uint32_t, std::uint32_t) override;
    void dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override;
    void transition(Texture&, ResourceState, ResourceState) override;
    void transition(Buffer&, ResourceState, ResourceState) override;
    void copyBuffer(Buffer&, Buffer&, std::size_t) override;
    void copyTextureToBuffer(Texture&, Buffer&, TextureSubresource = {}) override;
};
class VulkanDevice final : public Device {
public:
    DeviceDesc creationDesc;
    VkInstance instance{};
    VkDebugUtilsMessengerEXT messenger{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceProperties properties{};
    VkDevice handle{};
    std::uint32_t graphicsFamily{}, presentFamily{};
    VkQueue graphicsQueue{}, presentQueue{};
    VkCommandPool pool{};
    VmaAllocator allocator{};
    DeviceCapabilities caps;
    std::string name;
    std::atomic_uint32_t errors{};
    std::vector<std::function<void()>> deferredCallbacks;
    bool validation{}, syncValidation{};
    explicit VulkanDevice(const DeviceDesc&);
    ~VulkanDevice() override;
    void cleanup() noexcept;
    const DeviceCapabilities& capabilities() const override { return caps; }
    TextureSupport textureSupport(Format, TextureUsage, TextureDimension = TextureDimension::Texture2D) const override;
    std::unique_ptr<Buffer> createBuffer(const BufferDesc&) override;
    std::unique_ptr<Buffer> uploadBuffer(std::span<const std::byte>, BufferUsage) override;
    std::unique_ptr<Texture> createTexture(const TextureDesc&) override;
    std::unique_ptr<Texture> uploadTexture(const TextureDesc&, std::span<const std::byte>) override;
    std::unique_ptr<Sampler> createSampler(const SamplerDesc&) override;
    std::unique_ptr<Shader> createShader(const ShaderDesc&) override;
    std::unique_ptr<BindingLayout> createBindingLayout(const BindingLayoutDesc&) override;
    std::unique_ptr<BindingSet> createBindingSet(BindingLayout&, std::span<const BindingWrite>) override;
    std::unique_ptr<PipelineLayout> createPipelineLayout(const PipelineLayoutDesc&) override;
    std::unique_ptr<RenderPass> createRenderPass(const RenderPassDesc&) override;
    std::unique_ptr<Framebuffer> createFramebuffer(const FramebufferDesc&) override;
    std::unique_ptr<GraphicsPipeline> createGraphicsPipeline(const GraphicsPipelineDesc&) override;
    std::unique_ptr<ComputePipeline> createComputePipeline(const ComputePipelineDesc&) override;
    void submitImmediate(const std::function<void(CommandList&)>&) override;
    void deferUntilIdle(std::function<void()> callback) override;
    TextureReadback readback(Texture&, ResourceState = ResourceState::Sampled, TextureSubresource = {}) override;
    void waitIdle() override;
    const std::string& deviceName() const override { return name; }
    std::uint32_t validationErrorCount() const override { return errors.load(); }
    void immediate(const std::function<void(VulkanCommands&)>&);
    void drainDeferred() noexcept;

private:
    void createInstance();
    void selectDevice();
    void createLogicalDevice();
    VkDebugUtilsMessengerCreateInfoEXT debugInfo();
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                                        VkDebugUtilsMessageTypeFlagsEXT,
                                                        const VkDebugUtilsMessengerCallbackDataEXT*, void*);
};
class VulkanSwapchain final : public Swapchain {
public:
    VulkanDevice* owner;
    VkSwapchainKHR handle{};
    VkFormat format{VK_FORMAT_UNDEFINED};
    Extent2D size{};
    std::uint32_t minimum{}, imageIndex{};
    bool rebuild{}, acquiredFrame{}, submitted{};
    std::unique_ptr<RenderPass> pass;
    std::unique_ptr<RenderPass> loadPass;
    std::vector<std::unique_ptr<VulkanFramebuffer>> frames;
    std::vector<std::unique_ptr<VulkanFramebuffer>> loadFrames;
    std::vector<std::unique_ptr<VulkanTexture>> images;
    std::vector<ResourceState> imageStates;
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> finished;
    VulkanCommands commands;
    VkFence fence{};
    VkSemaphore acquired{};
    explicit VulkanSwapchain(VulkanDevice&);
    ~VulkanSwapchain() override;
    bool beginFrame() override;
    CommandList& commandList() override;
    RenderPass& renderPass() override { return *pass; }
    Framebuffer& framebuffer() override;
    RenderPass& loadRenderPass() override { return *loadPass; }
    Framebuffer& loadFramebuffer() override;
    Extent2D extent() const override { return size; }
    std::uint32_t minimumImageCount() const override { return minimum; }
    std::uint32_t imageCount() const override { return static_cast<std::uint32_t>(frames.size()); }
    Texture& image() override;
    ResourceState imageState() const override;
    void submit() override;
    void present() override;
    void create();
    void destroyImages() noexcept;
    void cleanup() noexcept;
};
VulkanDevice& nativeDevice(Device&);
}
