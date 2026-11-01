#include <VulkanBackend.hpp>
#include "shaders/ShaderLibrary.hpp"
#include "scene/GpuModel.hpp"
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace aeg;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <class F> void rejects(F action, const char* message) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}

// Face bytes are independently tagged, so copying one face six times or using
// a layer's byte stride as a row stride cannot pass this round trip.
void cubeUpload(rhi::Device& device) {
    constexpr auto usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopySource;
    const rhi::TextureDesc description{{2, 2}, rhi::Format::Rgba8Unorm, usage, 1, 1, rhi::TextureDimension::Cube};
    std::array<std::uint8_t, 6 * 2 * 2 * 4> pixels{};
    for (unsigned face = 0; face < 6; ++face)
        for (unsigned pixel = 0; pixel < 4; ++pixel) {
            const auto offset = (face * 4 + pixel) * 4;
            pixels[offset] = static_cast<std::uint8_t>(20 * face + pixel);
            pixels[offset + 1] = static_cast<std::uint8_t>(240 - 20 * face);
            pixels[offset + 2] = static_cast<std::uint8_t>(50 + 2 * pixel);
            pixels[offset + 3] = 255;
        }
    auto cube = device.uploadTexture(description, std::as_bytes(std::span(pixels)));
    for (unsigned face = 0; face < 6; ++face) {
        const auto result = device.readback(*cube, rhi::ResourceState::Sampled, {0, face});
        require(result.extent == rhi::Extent2D{2, 2} && result.format == rhi::Format::Rgba8Unorm &&
                    result.pixels.size() == 16,
                "Cube face upload/readback returned the wrong footprint");
        require(std::memcmp(result.pixels.data(), pixels.data() + face * 16, 16) == 0,
                "Packed cube upload changed face order, byte stride or values");
    }
    rejects([&] { (void)device.readback(*cube, rhi::ResourceState::Sampled, {0, 6}); },
            "Out-of-range cube face was read");
    rejects([&] { (void)device.readback(*cube, rhi::ResourceState::Sampled, {1, 0}); }, "Unprovided cube mip was read");
    rejects([&] { (void)device.uploadTexture(description, std::as_bytes(std::span(pixels)).first(16)); },
            "Single-face data initialized a declared six-face cube");
    auto generatedMips = description;
    generatedMips.mipLevels = 2;
    auto generatedCube = device.uploadTexture(generatedMips, std::as_bytes(std::span(pixels)));
    for (unsigned face = 0; face < 6; ++face) {
        const auto result = device.readback(*generatedCube, rhi::ResourceState::Sampled, {1, face});
        require(result.extent == rhi::Extent2D{1, 1} && result.format == rhi::Format::Rgba8Unorm &&
                    result.pixels.size() == 4,
                "Generated cube mip chain returned the wrong extent, format or pixel size");
    }

    auto floatDescription = description;
    floatDescription.format = rhi::Format::Rgba32Float;
    std::array<float, 6 * 2 * 2 * 4> hdr{};
    for (unsigned face = 0; face < 6; ++face)
        for (unsigned pixel = 0; pixel < 4; ++pixel) {
            const auto offset = (face * 4 + pixel) * 4;
            hdr[offset] = static_cast<float>(face + 2);
            hdr[offset + 1] = static_cast<float>(pixel) + 0.25f;
            hdr[offset + 2] = 5.5f;
            hdr[offset + 3] = 1;
        }
    auto hdrCube = device.uploadTexture(floatDescription, std::as_bytes(std::span(hdr)));
    for (unsigned face = 0; face < 6; ++face) {
        const auto result = device.readback(*hdrCube, rhi::ResourceState::Sampled, {0, face});
        require(result.pixels.size() == 16 * sizeof(float) &&
                    std::memcmp(result.pixels.data(), hdr.data() + face * 16, result.pixels.size()) == 0,
                "Floating-point cube upload clipped HDR values or selected the wrong face");
    }
}

struct Push {
    std::uint32_t size{}, samples{};
    float roughness{};
    std::uint32_t sourceSize{};
};
static_assert(sizeof(Push) == 16);

void computeCommands(rhi::Device& device) {
    constexpr auto usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage | rhi::TextureUsage::CopySource;
    auto bindings = device.createBindingLayout({{{1, rhi::BindingType::StorageImage, rhi::ShaderStage::Compute}}});
    auto layout = device.createPipelineLayout({{bindings.get()}, sizeof(Push), rhi::ShaderStage::Compute});
    auto shader = device.createShader({rhi::ShaderStage::Compute, rhi::ShaderFormat::Spirv,
                                       render::detail::embeddedShader(render::detail::ShaderId::BrdfCompute)});
    auto pipeline = device.createComputePipeline({layout.get(), shader.get()});
    auto lut = device.createTexture({{4, 4}, rhi::Format::Rgba32Float, usage, 1, 3});
    std::array<std::unique_ptr<rhi::BindingSet>, 3> sets;
    for (unsigned mip = 0; mip < sets.size(); ++mip) {
        const std::array writes{rhi::BindingWrite{1, nullptr, lut.get(), nullptr, mip}};
        sets[mip] = device.createBindingSet(*bindings, writes);
    }
    rejects(
        [&] {
            const std::array writes{rhi::BindingWrite{1, nullptr, lut.get(), nullptr, 3}};
            (void)device.createBindingSet(*bindings, writes);
        },
        "Storage descriptor accepted an out-of-range mip");
    auto wrongStage = device.createShader({rhi::ShaderStage::Vertex, rhi::ShaderFormat::Spirv,
                                           render::detail::embeddedShader(render::detail::ShaderId::FullscreenVertex)});
    rejects([&] { (void)device.createComputePipeline({layout.get(), wrongStage.get()}); },
            "Compute pipeline accepted vertex-stage shader metadata");
    auto graphicsPush = device.createPipelineLayout({{bindings.get()}, sizeof(Push), rhi::ShaderStage::Fragment});
    rejects([&] { (void)device.createComputePipeline({graphicsPush.get(), shader.get()}); },
            "Compute pipeline accepted graphics-only push visibility");
    rejects([&] { device.submitImmediate({}); }, "Empty immediate callback was accepted");
    rejects([&] { device.submitImmediate([](rhi::CommandList& commands) { commands.dispatch(1, 1, 1); }); },
            "Dispatch without a compute pipeline was recorded");
    rejects(
        [&] {
            device.submitImmediate([&](rhi::CommandList& commands) {
                commands.bindComputePipeline(*pipeline);
                commands.dispatch(1, 1, 1);
            });
        },
        "Dispatch without its descriptor set was recorded");
    auto uninitialized = device.createBindingSet(*bindings, {});
    rejects(
        [&] {
            device.submitImmediate([&](rhi::CommandList& commands) {
                commands.bindComputePipeline(*pipeline);
                commands.bindSet(*layout, 0, *uninitialized, rhi::PipelineBindPoint::Compute);
                commands.dispatch(1, 1, 1);
            });
        },
        "Dispatch accepted an uninitialized storage descriptor");
    auto attachment = device.createTexture({{4, 4}, rhi::Format::Rgba8Unorm, rhi::TextureUsage::ColorAttachment});
    auto pass = device.createRenderPass({{{rhi::Format::Rgba8Unorm, 1, rhi::LoadOp::Discard, rhi::StoreOp::Discard,
                                           rhi::ResourceState::Undefined, rhi::ResourceState::ColorAttachment}},
                                         {0},
                                         {},
                                         {}});
    auto framebuffer = device.createFramebuffer({pass.get(), {attachment.get()}, {4, 4}});
    rejects(
        [&] {
            device.submitImmediate([&](rhi::CommandList& commands) {
                commands.bindComputePipeline(*pipeline);
                commands.bindSet(*layout, 0, *sets[0], rhi::PipelineBindPoint::Compute);
                commands.beginRenderPass(*pass, *framebuffer, {4, 4}, {});
                commands.dispatch(1, 1, 1);
            });
        },
        "Dispatch was recorded inside an active render pass");
    for (unsigned axis = 0; axis < 3; ++axis) {
        rejects(
            [&] {
                device.submitImmediate([&](rhi::CommandList& commands) {
                    commands.bindComputePipeline(*pipeline);
                    commands.bindSet(*layout, 0, *sets[0], rhi::PipelineBindPoint::Compute);
                    std::array<std::uint32_t, 3> groups{1, 1, 1};
                    groups[axis] = 0;
                    commands.dispatch(groups[0], groups[1], groups[2]);
                });
            },
            "Dispatch accepted a zero group dimension");
        const auto maximum = device.capabilities().maximumComputeGroupCount[axis];
        if (maximum != std::numeric_limits<std::uint32_t>::max())
            rejects(
                [&] {
                    device.submitImmediate([&](rhi::CommandList& commands) {
                        commands.bindComputePipeline(*pipeline);
                        commands.bindSet(*layout, 0, *sets[0], rhi::PipelineBindPoint::Compute);
                        std::array<std::uint32_t, 3> groups{1, 1, 1};
                        groups[axis] = maximum + 1;
                        commands.dispatch(groups[0], groups[1], groups[2]);
                    });
                },
                "Dispatch accepted a group dimension above the device limit");
    }
    // An exception after recording must discard the command buffer. The next
    // successful submission can still initialize this image from Undefined.
    bool callbackEntered{}, callbackThrew{};
    try {
        device.submitImmediate([&](rhi::CommandList& commands) {
            callbackEntered = true;
            commands.transition(*lut, rhi::ResourceState::Undefined, rhi::ResourceState::Storage);
            throw std::runtime_error("Intentional abandoned recording");
        });
    } catch (const std::runtime_error&) {
        if (!callbackEntered) throw;
        callbackThrew = true;
    }
    require(callbackThrew, "Immediate recording swallowed the callback exception");
    device.submitImmediate([&](rhi::CommandList& commands) {
        commands.transition(*lut, rhi::ResourceState::Undefined, rhi::ResourceState::Storage);
        commands.bindComputePipeline(*pipeline);
        for (unsigned mip = 0; mip < sets.size(); ++mip) {
            commands.bindSet(*layout, 0, *sets[mip], rhi::PipelineBindPoint::Compute);
            const Push push{4u >> mip, 256, 0, 0};
            commands.pushConstants(*layout, std::as_bytes(std::span(&push, 1)));
            commands.dispatch(1, 1, 1);
            if (mip == 0) {
                // The exact same subresource is overwritten, exercising the
                // same-layout WAW barrier rather than just disjoint mip writes.
                commands.transition(*lut, rhi::ResourceState::Storage, rhi::ResourceState::Storage);
                commands.dispatch(1, 1, 1);
            }
        }
        commands.transition(*lut, rhi::ResourceState::Storage, rhi::ResourceState::Sampled);
    });
    for (unsigned mip = 0; mip < sets.size(); ++mip) {
        const auto result = device.readback(*lut, rhi::ResourceState::Sampled, {mip, 0});
        const unsigned size = 4u >> mip;
        require(result.extent == rhi::Extent2D{size, size} && result.pixels.size() == size * size * 4 * sizeof(float),
                "Selected LUT mip readback used the base-level extent");
        std::vector<float> values(result.pixels.size() / sizeof(float));
        std::memcpy(values.data(), result.pixels.data(), result.pixels.size());
        for (std::size_t pixel = 0; pixel < values.size(); pixel += 4)
            require(std::isfinite(values[pixel]) && values[pixel] > 0 && values[pixel] < 2 &&
                        std::isfinite(values[pixel + 1]) && values[pixel + 1] >= 0 && values[pixel + 1] < 2 &&
                        values[pixel + 2] == 0 && values[pixel + 3] == 1,
                    "Compute storage mip write/readback produced invalid LUT values");
    }
    rejects([&] { (void)device.readback(*lut, rhi::ResourceState::Sampled, {3, 0}); },
            "LUT readback accepted an invalid mip");
    rejects([&] { (void)device.readback(*lut, rhi::ResourceState::Sampled, {0, 1}); },
            "2D readback accepted a second layer");
    auto staging = device.createBuffer({16, rhi::BufferUsage::CopyDestination, rhi::MemoryUsage::Readback});
    rejects(
        [&] {
            device.submitImmediate(
                [&](rhi::CommandList& commands) { commands.copyTextureToBuffer(*lut, *staging, {0, 0}); });
        },
        "Subresource copy accepted an undersized destination buffer");
}
void cullingCommands(rhi::Device& device) {
    const auto& caps = device.capabilities();
    if (!caps.compute || !caps.indirectDraw || !caps.descriptorIndexing || !caps.maximumBindlessSampledDescriptors ||
        !caps.shaderDrawParameters) {
        std::cout << "SKIP GPU culling: compute=" << caps.compute << " indirect=" << caps.indirectDraw
                  << " descriptorIndexing=" << caps.descriptorIndexing
                  << " shaderDrawParameters=" << caps.shaderDrawParameters
                  << " maxSampled=" << caps.maximumBindlessSampledDescriptors << '\n';
        return;
    }
    using namespace aeg::render;
    using enum rhi::BindingType;
    using enum rhi::ShaderStage;
    SceneUniform scene{};
    scene.viewProjection = math::identity();
    scene.model = math::identity();
    scene.currentViewProjection = math::identity();
    auto uniform = device.createBuffer({sizeof(SceneUniform), rhi::BufferUsage::Uniform, rhi::MemoryUsage::Upload});
    std::memcpy(uniform->mapped().data(), &scene, sizeof(scene));
    auto set0Layout = device.createBindingLayout({{{0, UniformBuffer, Vertex | Fragment | Compute}}});
    auto cullLayout = device.createBindingLayout({{{0, StorageBuffer, Compute}, {1, StorageBuffer, Compute}}});
    auto pipelineLayout = device.createPipelineLayout({{set0Layout.get(), cullLayout.get()}, sizeof(GpuCullPush), Compute});
    auto shader = device.createShader({rhi::ShaderStage::Compute, rhi::ShaderFormat::Spirv,
                                       detail::embeddedShader(detail::ShaderId::CullCompute)});
    auto pipeline = device.createComputePipeline({pipelineLayout.get(), shader.get()});
    std::array<GpuCullBounds, 2> bounds{};
    bounds[0].localMinimum = {-0.25f, -0.25f, 0.25f, 0};
    bounds[0].localMaximum = {0.25f, 0.25f, 0.75f, 0};
    bounds[0].draw = {3, 0, 0, 4};
    bounds[1].localMinimum = {2, 2, 2, 0};
    bounds[1].localMaximum = {3, 3, 3, 0};
    bounds[1].draw = {6, 0, 0, 7};
    auto boundsBuffer = device.uploadBuffer(std::as_bytes(std::span(bounds)), rhi::BufferUsage::Storage);
    auto indirect = device.createBuffer({2 * sizeof(std::uint32_t) * 5,
                                         rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect |
                                             rhi::BufferUsage::CopySource,
                                         rhi::MemoryUsage::Device});
    auto readback = device.createBuffer({2 * sizeof(std::uint32_t) * 5, rhi::BufferUsage::CopyDestination,
                                         rhi::MemoryUsage::Readback});
    const std::array set0Writes{rhi::BindingWrite{0, uniform.get(), nullptr, nullptr}};
    const std::array cullWrites{rhi::BindingWrite{0, boundsBuffer.get(), nullptr, nullptr},
                                rhi::BindingWrite{1, indirect.get(), nullptr, nullptr}};
    auto set0 = device.createBindingSet(*set0Layout, set0Writes);
    auto cullSet = device.createBindingSet(*cullLayout, cullWrites);
    device.submitImmediate([&](rhi::CommandList& commands) {
        commands.transition(*boundsBuffer, rhi::ResourceState::Storage, rhi::ResourceState::Storage);
        commands.transition(*indirect, rhi::ResourceState::Indirect, rhi::ResourceState::Storage);
        commands.bindComputePipeline(*pipeline);
        commands.bindSet(*pipelineLayout, 0, *set0, rhi::PipelineBindPoint::Compute);
        commands.bindSet(*pipelineLayout, 1, *cullSet, rhi::PipelineBindPoint::Compute);
        math::Frustum frustum;
        require(math::try_extract_frustum(math::identity(), frustum, {true, false}),
                "GPU culling test could not build an identity frustum");
        GpuCullPush push{};
        for (std::size_t i = 0; i < push.planes.size(); ++i)
            push.planes[i] = {frustum.planes[i].normal, frustum.planes[i].offset};
        push.meshCount = 2;
        commands.pushConstants(*pipelineLayout, std::as_bytes(std::span(&push, 1)));
        commands.dispatch(1, 1, 1);
        commands.transition(*indirect, rhi::ResourceState::Storage, rhi::ResourceState::Indirect);
        commands.transition(*indirect, rhi::ResourceState::Indirect, rhi::ResourceState::CopySource);
        commands.copyBuffer(*indirect, *readback, 2 * sizeof(std::uint32_t) * 5);
    });
    std::array<std::uint32_t, 10> commands{};
    std::memcpy(commands.data(), readback->mapped().data(), sizeof(commands));
    std::cout << "GPU cull commands: " << commands[0] << ',' << commands[1] << ',' << commands[4] << " | "
              << commands[5] << ',' << commands[6] << ',' << commands[9] << '\n';
    require(commands[0] == 3 && commands[1] == 1 && commands[4] == 4 && commands[5] == 0 && commands[6] == 0,
            "GPU culling changed the visible indirect command");
    require(commands[7] == 0 && commands[8] == 0 && commands[9] == 0,
            "GPU culling failed to zero the culled indirect command");
    cullSet.reset();
    set0.reset();
}
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(64, 64, "AEG RHI compute contracts", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    std::ostringstream diagnostics;
    auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
    int result{};
    try {
        rhi::NativeWindow native;
        native.window = glfwGetWin32Window(window);
        native.application = GetModuleHandleW(nullptr);
        native.framebufferExtent = [] { return rhi::Extent2D{64, 64}; };
        auto device = rhi::vulkan::createVulkanDevice({native, true});
        require(device->capabilities().compute, "Test GPU has no graphics-family compute support");
        cubeUpload(*device);
        computeCommands(*device);
        cullingCommands(*device);
        device->waitIdle();
        require(device->validationErrorCount() == 0, "RHI compute contracts produced Vulkan validation errors");
        device.reset();
        require(diagnostics.str().find("[Vulkan error]") == std::string::npos,
                "RHI compute contracts produced a validation error during teardown");
        std::cout << "PASS RHI compute: packed byte/HDR cubes, face/mip copies, dispatch/stage/descriptor bounds, "
                     "AABB culling indirect commands, abandoned recording, repeated storage barrier and completed LUT "
                     "writes; Vulkan errors: 0\n";
    } catch (const std::exception& error) {
        std::cout << "FAIL RHI compute contracts: " << error.what() << '\n';
        result = 1;
    }
    std::cerr.rdbuf(previous);
    if (result) std::cerr << diagnostics.str();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
