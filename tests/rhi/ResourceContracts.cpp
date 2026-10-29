#include <RhiDevice.hpp>
#include <RhiSwapchain.hpp>
#include "fake/FakeDevice.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
struct FakeSwapchainContract final : aeg::rhi::Swapchain {
    aeg::tests::FakeTexture texture{{{64, 64}, aeg::rhi::Format::Rgba8Unorm,
                                     aeg::rhi::TextureUsage::ColorAttachment}};
    aeg::tests::FakeCommands commands;
    aeg::tests::FakePass pass{{{{aeg::rhi::Format::Rgba8Unorm, 1, aeg::rhi::LoadOp::Clear,
                                 aeg::rhi::StoreOp::Store, aeg::rhi::ResourceState::ColorAttachment,
                                 aeg::rhi::ResourceState::ColorAttachment}},
                               {0}, std::nullopt, {}}};
    aeg::tests::FakeFramebuffer frame{{&pass, {&texture}, {64, 64}}};
    aeg::rhi::ResourceState state{aeg::rhi::ResourceState::Undefined};
    bool beginFrame() override { return true; }
    aeg::rhi::CommandList& commandList() override { return commands; }
    aeg::rhi::RenderPass& renderPass() override { return pass; }
    aeg::rhi::Framebuffer& framebuffer() override { return frame; }
    aeg::rhi::Extent2D extent() const override { return {64, 64}; }
    std::uint32_t minimumImageCount() const override { return 2; }
    std::uint32_t imageCount() const override { return 2; }
    void submit() override {}
    void present() override { state = aeg::rhi::ResourceState::Present; }
    aeg::rhi::Texture& image() override { return texture; }
    aeg::rhi::ResourceState imageState() const override { return state; }
};
void swapchainImageContracts() {
    FakeSwapchainContract swapchain;
    if (&swapchain.image() != &swapchain.texture || swapchain.imageState() != aeg::rhi::ResourceState::Undefined)
        throw std::runtime_error("Swapchain image contract did not expose Undefined image");
    swapchain.present();
    if (swapchain.imageState() != aeg::rhi::ResourceState::Present)
        throw std::runtime_error("Swapchain image state did not transition to Present");
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Invalid RHI description accepted");
}
void computeImageContracts() {
    using namespace aeg::rhi;
    // A wrong mip footprint or treating a cube as one layer makes real selected-mip
    // readback and storage dispatch writes address the wrong memory.
    TextureDesc cube{
        {16, 16}, Format::Rgba32Float,   TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::CopySource, 1,
        5,        TextureDimension::Cube};
    if (mipExtent(cube, 0) != Extent2D{16, 16} || mipExtent(cube, 3) != Extent2D{2, 2} ||
        mipExtent(cube, 4) != Extent2D{1, 1} || textureLayerCount(cube) != 6)
        throw std::runtime_error("Cube mip footprints or face count are incorrect");
    validate(cube);
    rejects([&] { (void)mipExtent(cube, 5); });
    auto invalid = cube;
    invalid.extent.height = 8;
    rejects([&] { validate(invalid); });
    invalid = cube;
    invalid.samples = 4;
    rejects([&] { validate(invalid); });
    invalid = cube;
    invalid.mipLevels = 6;
    rejects([&] { validate(invalid); });
    invalid = cube;
    invalid.mipLevels = 0;
    rejects([&] { validate(invalid); });
    invalid = cube;
    invalid.format = Format::D32Float;
    rejects([&] { validate(invalid); });
    invalid = cube;
    invalid.dimension = static_cast<TextureDimension>(99);
    rejects([&] { validate(invalid); });
    TextureDesc rectangle{{9, 3}, Format::Rgba32Float, TextureUsage::Storage, 1, 4};
    validate(rectangle);
    if (textureLayerCount(rectangle) != 1 || mipExtent(rectangle, 1) != Extent2D{4, 1} ||
        mipExtent(rectangle, 3) != Extent2D{1, 1})
        throw std::runtime_error("Non-power-of-two mip footprint is incorrect");
    rectangle.samples = 2;
    rejects([&] { validate(rectangle); });
    SamplerDesc sampler{Filter::Linear, AddressMode::Repeat, AddressMode::ClampToEdge, 4.f};
    validate(sampler);
    sampler.maxLod = -1;
    rejects([&] { validate(sampler); });
    sampler.maxLod = std::numeric_limits<float>::infinity();
    rejects([&] { validate(sampler); });
    sampler.maxLod = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { validate(sampler); });
    sampler.maxLod = 0;
    sampler.addressV = static_cast<AddressMode>(99);
    rejects([&] { validate(sampler); });
    struct Layout : PipelineLayout {
    } layout;
    struct ComputeShader : Shader {
    } shader;
    validate(ComputePipelineDesc{&layout, &shader});
    rejects([&] { validate(ComputePipelineDesc{nullptr, &shader}); });
    rejects([&] { validate(ComputePipelineDesc{&layout, nullptr}); });
}
void depthOnlyContracts() {
    using namespace aeg::rhi;
    for (const auto format : {Format::D32Float, Format::D16Unorm, Format::D24UnormS8}) {
        validate(TextureDesc{{128, 128}, format, TextureUsage::DepthAttachment | TextureUsage::Sampled, 1});
        RenderPassDesc shadow;
        shadow.attachments = {
            {format, 1, LoadOp::Clear, StoreOp::Store, ResourceState::Undefined, ResourceState::Sampled}};
        shadow.depth = 0;
        validate(shadow);
        // A depth-only pass still validates sample counts without a color attachment to compare against.
        shadow.attachments[0].samples = 4;
        validate(shadow);
        shadow.attachments[0].samples = 3;
        rejects([&] { validate(shadow); });
        shadow.attachments[0].samples = 1;
        auto invalid = shadow;
        invalid.depth.reset();
        rejects([&] { validate(invalid); });
        invalid = shadow;
        invalid.depth = 1;
        rejects([&] { validate(invalid); });
        invalid = shadow;
        invalid.attachments[0].format = Format::R32Float;
        rejects([&] { validate(invalid); });
        invalid = shadow;
        invalid.attachments[0].load = LoadOp::Load;
        rejects([&] { validate(invalid); });
        shadow.attachments[0].load = LoadOp::Load;
        shadow.attachments[0].initialState = ResourceState::Sampled;
        validate(shadow);
        invalid = shadow;
        invalid.resolves = {unusedAttachment};
        rejects([&] { validate(invalid); });
        invalid = shadow;
        invalid.colors = {0};
        rejects([&] { validate(invalid); });
    }
    rejects([] { validate(RenderPassDesc{}); });
    GraphicsPipelineDesc shadowPipeline;
    shadowPipeline.colorAttachmentCount = 0;
    shadowPipeline.depthTest = shadowPipeline.depthWrite = true;
    validate(shadowPipeline, false);
    shadowPipeline.samples = 4;
    validate(shadowPipeline, false);
    shadowPipeline.samples = 3;
    rejects([&] { validate(shadowPipeline, false); });
    shadowPipeline.samples = 1;
    shadowPipeline.colorBlends = {BlendMode::Opaque};
    rejects([&] { validate(shadowPipeline, false); });
    shadowPipeline.colorBlends = {BlendMode::Alpha};
    rejects([&] { validate(shadowPipeline, true); });
}
}
int main() {
    using namespace aeg::rhi;
    try {
        computeImageContracts();
        validate(BufferDesc{32, BufferUsage::Uniform, MemoryUsage::Upload});
        rejects([] { validate(BufferDesc{}); });
        rejects([] { validate(BufferDesc{32, BufferUsage::None}); });
        validate(TextureDesc{{64, 32}, Format::Rgba8Unorm, TextureUsage::Sampled});
        rejects([] { validate(TextureDesc{{0, 32}}); });
        rejects([] { validate(TextureDesc{{64, 32}, Format::Rgba8Unorm, TextureUsage::ColorAttachment, 3}); });
        rejects([] { validate(TextureDesc{{64, 32}, Format::D32Float, TextureUsage::ColorAttachment}); });
        rejects([] { validate(TextureDesc{{64, 32}, Format::Rgba8Unorm, TextureUsage::DepthAttachment}); });
        depthOnlyContracts();
        swapchainImageContracts();
        RenderPassDesc pass;
        pass.attachments = {
            {Format::Rgba8Unorm, 4, LoadOp::Clear, StoreOp::Discard, ResourceState::Undefined,
             ResourceState::ColorAttachment},
            {Format::D32Float, 4, LoadOp::Clear, StoreOp::Discard, ResourceState::Undefined,
             ResourceState::DepthAttachment},
            {Format::Rgba8Unorm, 1, LoadOp::Discard, StoreOp::Store, ResourceState::Undefined, ResourceState::Sampled}};
        pass.colors = {0};
        pass.depth = 1;
        pass.resolves = {2};
        validate(pass);
        auto invalid = pass;
        invalid.resolves = {0};
        rejects([&] { validate(invalid); });
        invalid = pass;
        invalid.attachments[2].samples = 4;
        rejects([&] { validate(invalid); });
        invalid = pass;
        invalid.depth = 3;
        rejects([&] { validate(invalid); });
        invalid = pass;
        invalid.attachments[0].load = LoadOp::Load;
        rejects([&] { validate(invalid); });
        invalid = pass;
        invalid.attachments[1].samples = 1;
        rejects([&] { validate(invalid); });
        invalid = pass;
        invalid.attachments.push_back({Format::Rgba8Unorm, 1});
        invalid.colors.push_back(3);
        invalid.resolves.push_back(unusedAttachment);
        rejects([&] { validate(invalid); });
        // Color-only passes and omitted resolve slots keep their existing contracts.
        auto colorOnly = pass;
        colorOnly.depth.reset();
        colorOnly.resolves = {unusedAttachment};
        validate(colorOnly);
        if (formatByteSize(Format::Rgba16Float) != 8 || formatByteSize(Format::Rgba32Float) != 16)
            throw std::runtime_error("Readback byte sizes changed");
        GraphicsPipelineDesc pipeline;
        pipeline.colorAttachmentCount = 2;
        validate(pipeline, false);
        pipeline.colorBlends = {BlendMode::Alpha, BlendMode::Opaque};
        rejects([&] { validate(pipeline, false); });
        validate(pipeline, true);
        pipeline.colorBlends = {BlendMode::Alpha, BlendMode::Alpha};
        validate(pipeline, false);
        pipeline.colorBlends = {BlendMode::Alpha};
        rejects([&] { validate(pipeline, true); });
        pipeline.colorBlends = {BlendMode::Opaque, static_cast<BlendMode>(99)};
        rejects([&] { validate(pipeline, true); });
        std::cout << "PASS standalone RHI resource contracts\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
