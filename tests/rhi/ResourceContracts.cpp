#include <RhiDevice.hpp>
#include <iostream>
#include <stdexcept>

namespace {
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Invalid RHI description accepted");
}
}
int main() {
    using namespace aeg::rhi;
    try {
        validate(BufferDesc{32, BufferUsage::Uniform, MemoryUsage::Upload});
        rejects([] { validate(BufferDesc{}); });
        rejects([] { validate(BufferDesc{32, BufferUsage::None}); });
        validate(TextureDesc{{64, 32}, Format::Rgba8Unorm, TextureUsage::Sampled});
        rejects([] { validate(TextureDesc{{0, 32}}); });
        rejects([] { validate(TextureDesc{{64, 32}, Format::Rgba8Unorm, TextureUsage::ColorAttachment, 3}); });
        rejects([] { validate(TextureDesc{{64, 32}, Format::D32Float, TextureUsage::ColorAttachment}); });
        rejects([] { validate(TextureDesc{{64, 32}, Format::Rgba8Unorm, TextureUsage::DepthAttachment}); });
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
