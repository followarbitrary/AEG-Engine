#include <RenderGraph.hpp>
#include "../../engine/render/private/rendergraph/TransientTexturePool.hpp"
#include "../rhi/fake/FakeDevice.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using Graph = aeg::render::RenderGraph;
using State = aeg::rhi::ResourceState;
using Pool = aeg::render::TransientTexturePool;
using FrameToken = aeg::render::FrameToken;

struct ContractSwapchain final : aeg::rhi::Swapchain {
    aeg::tests::FakeTexture imageTexture;
    aeg::tests::FakePass pass;
    aeg::tests::FakeFramebuffer framebufferObject;
    aeg::tests::FakeCommands commands;
    aeg::rhi::Extent2D frameExtent{16, 16};
    State state{State::Undefined};

    ContractSwapchain()
        : imageTexture({{16, 16}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment}),
          pass({{{aeg::rhi::Format::Rgba8Unorm, 1, aeg::rhi::LoadOp::Clear, aeg::rhi::StoreOp::Store,
                  State::ColorAttachment, State::ColorAttachment}}, {0}, std::nullopt, {}}),
          framebufferObject({&pass, {&imageTexture}, frameExtent}), commands(nullptr) {}

    bool beginFrame() override { return true; }
    aeg::rhi::CommandList& commandList() override { return commands; }
    aeg::rhi::RenderPass& renderPass() override { return pass; }
    aeg::rhi::Framebuffer& framebuffer() override { return framebufferObject; }
    aeg::rhi::Extent2D extent() const override { return frameExtent; }
    std::uint32_t minimumImageCount() const override { return 1; }
    std::uint32_t imageCount() const override { return 1; }
    aeg::rhi::Texture& image() override { return imageTexture; }
    State imageState() const override { return state; }
    void submit() override {}
    void present() override { state = State::Present; }
};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <class F> void rejects(F&& action, const char* message) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}
template <class F> void rejectsAny(F&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void usageAndBarriers() {
    Graph graph;
    const auto image = graph.createTexture({{64, 64}, aeg::rhi::Format::Rgba8Unorm,
                                            aeg::rhi::TextureUsage::ColorAttachment |
                                                aeg::rhi::TextureUsage::Sampled},
                                           "image", false);
    graph.addPass("clear", Graph::PassType::Graphics, {}, {}).write(image, Graph::Usage::ColorWrite);
    graph.addPass("sample", Graph::PassType::Graphics, {}, {}).read(image, Graph::Usage::SampledRead);
    const auto result = graph.compile(std::span<const Graph::Handle>(&image, 1));
    require(result.passes.size() == 2, "Color writer and sampled reader were not retained");
    require(result.passes[0].preBarriers.size() == 1 && result.passes[0].preBarriers[0].before == State::Undefined &&
                result.passes[0].preBarriers[0].after == State::ColorAttachment,
            "Color writer did not receive its initial barrier");
    require(result.passes[1].preBarriers.size() == 1 && result.passes[1].preBarriers[0].before == State::ColorAttachment &&
                result.passes[1].preBarriers[0].after == State::Sampled,
            "Sampled reader did not receive the writer-to-reader barrier");
}

void orderingCullingAndDump() {
    Graph graph;
    const auto output = graph.createTexture({{16, 16}, aeg::rhi::Format::Rgba8Unorm,
                                              aeg::rhi::TextureUsage::ColorAttachment | aeg::rhi::TextureUsage::Sampled},
                                             "output", false);
    const auto dead = graph.createTexture({{16, 16}, aeg::rhi::Format::Rgba8Unorm,
                                           aeg::rhi::TextureUsage::ColorAttachment},
                                          "dead", false);
    graph.addPass("first", Graph::PassType::Graphics, {}, {}).write(output, Graph::Usage::ColorWrite);
    graph.addPass("dead", Graph::PassType::Graphics, {}, {}).write(dead, Graph::Usage::ColorWrite);
    graph.addPass("last", Graph::PassType::Graphics, {}, {}).read(output, Graph::Usage::SampledRead);
    const auto result = graph.compile(std::span<const Graph::Handle>(&output, 1));
    require(result.passes.size() == 2, "Unreachable pass was not culled");
    require(result.passes[0].name == "first" && result.passes[1].name == "last", "Pass order was not stable");
    const auto dump = graph.debugDump();
    require(dump.find("first") != std::string::npos && dump.find("dead") != std::string::npos &&
                dump.find("ColorAttachment") != std::string::npos,
            "Graph debug dump omitted pass or barrier details");
}

void validationContracts() {
    Graph graph;
    const auto sampled = graph.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm,
                                               aeg::rhi::TextureUsage::Sampled},
                                              "sampled", false);
    rejects([&] { graph.addPass("undefined", Graph::PassType::Graphics, {}, {}).read(sampled, Graph::Usage::SampledRead); graph.compile({}); },
            "Undefined transient read was accepted");

    Graph storage;
    const auto storageImage = storage.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm,
                                                       aeg::rhi::TextureUsage::Storage},
                                                      "storage", false);
    storage.addPass("storage", Graph::PassType::Compute, {}, {})
        .readWrite(storageImage, Graph::Usage::StorageReadWrite);
    const auto storageResult = storage.compile({storageImage});
    require(storageResult.passes.size() == 1 && storageResult.passes[0].preBarriers[0].after == State::Storage,
            "Storage read-write did not map to GENERAL storage state");

    Graph cycle;
    const auto a = cycle.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::Storage}, "a", false);
    const auto b = cycle.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::Storage}, "b", false);
    cycle.addPass("A", Graph::PassType::Compute, {}, {}).write(a, Graph::Usage::StorageWrite).read(b, Graph::Usage::StorageRead);
    cycle.addPass("B", Graph::PassType::Compute, {}, {}).write(b, Graph::Usage::StorageWrite).read(a, Graph::Usage::StorageRead);
    rejects([&] { cycle.compile({a}); }, "Dependency cycle was accepted");

    Graph future;
    const auto futureImage = future.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm,
                                                     aeg::rhi::TextureUsage::Storage},
                                                    "future", false);
    future.addPass("reader-before-writer", Graph::PassType::Compute, {}, {})
        .read(futureImage, Graph::Usage::StorageRead);
    future.addPass("future-writer", Graph::PassType::Compute, {}, {})
        .write(futureImage, Graph::Usage::StorageWrite);
    rejects([&] { future.compile({futureImage}); }, "Read was silently reordered after a future writer");

    Graph present;
    const auto regular = present.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm,
                                                 aeg::rhi::TextureUsage::ColorAttachment},
                                                "regular", false);
    present.addPass("present", Graph::PassType::Graphics, {}, {}).write(regular, Graph::Usage::Present);
    rejects([&] { present.compile(std::span<const Graph::Handle>(&regular, 1)); },
            "Present usage was accepted for a non-swapchain resource");
}

void generationContract() {
    Graph graph;
    const auto old = graph.createTexture({{1, 1}, aeg::rhi::Format::Rgba8Unorm,
                                          aeg::rhi::TextureUsage::ColorAttachment},
                                         "old", false);
    graph.reset();
    const auto fresh = graph.createTexture({{1, 1}, aeg::rhi::Format::Rgba8Unorm,
                                            aeg::rhi::TextureUsage::ColorAttachment},
                                           "fresh", false);
    require(old != fresh, "Reset did not change handle generation");
    rejects([&] { graph.addPass("stale", Graph::PassType::Graphics, {}, {}).write(old, Graph::Usage::ColorWrite); graph.compile({}); },
            "Stale handle was accepted after reset");
}

void transientPoolContracts() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    const aeg::rhi::TextureDesc desc{{32, 32}, aeg::rhi::Format::Rgba8Unorm,
                                     aeg::rhi::TextureUsage::ColorAttachment | aeg::rhi::TextureUsage::Sampled};
    auto& first = pool.acquire(desc);
    pool.retire(first, FrameToken{7});
    pool.reclaimCompletedFrame(FrameToken{7});
    auto& beforeCompletion = pool.acquire(desc);
    require(&beforeCompletion != &first, "Transient pool reused a texture before completion");
    pool.retire(beforeCompletion, FrameToken{8});
    pool.reclaimCompletedFrame(FrameToken{9});
    auto& afterCompletion = pool.acquire(desc);
    require(&afterCompletion == &first || &afterCompletion == &beforeCompletion,
            "Transient pool did not reuse a compatible completed texture");
    require(pool.poolHitCount() == 1, "Transient pool hit count did not report one reuse");
    const aeg::rhi::TextureDesc incompatible{{32, 32}, aeg::rhi::Format::Rgba16Float,
                                              aeg::rhi::TextureUsage::ColorAttachment | aeg::rhi::TextureUsage::Sampled};
    auto& other = pool.acquire(incompatible);
    require(&other != &afterCompletion, "Transient pool reused an incompatible texture");
}

void executionContracts() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    aeg::tests::FakeCommands commands(&device.caps);
    Graph graph;
    const auto image = graph.createTexture(
        {{32, 32}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment}, "transient", true);
    bool callbackSawRenderPass = false;
    aeg::rhi::Texture* callbackTexture = nullptr;
    graph.addPass("draw", Graph::PassType::Graphics, {}, [&](Graph::PassContext& context) {
        callbackTexture = &context.texture(image);
        callbackSawRenderPass = commands.inRenderPass();
    }).write(image, Graph::Usage::ColorWrite);
    const auto compiled = graph.compile({image});
    require(compiled.resources[0].firstUse == 0 && compiled.resources[0].lastUse == 0,
            "Transient first/last-use metadata was not recorded");
    graph.execute(commands, device, pool, FrameToken{1});
    require(callbackSawRenderPass && callbackTexture != nullptr, "PassContext did not resolve a physical texture");
    require(!commands.transitions.empty(), "Graph execution emitted no pre-barrier");
    require(!commands.inRenderPass(), "Graphics pass remained active after execution");
    pool.reclaimCompletedFrame(FrameToken{2});

    Graph failing;
    const auto failingImage = failing.createTexture(
        {{32, 32}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment}, "failed", true);
    aeg::rhi::Texture* failedTexture = nullptr;
    failing.addPass("fail", Graph::PassType::Graphics, {}, [&](Graph::PassContext& context) {
        failedTexture = &context.texture(failingImage);
        throw std::runtime_error("record failure");
    }).write(failingImage, Graph::Usage::ColorWrite);
    failing.compile({failingImage});
    aeg::tests::FakeCommands failedCommands(&device.caps);
    try {
        failing.execute(failedCommands, device, pool, FrameToken{5});
    } catch (const std::runtime_error&) {
    }
    require(failedTexture != nullptr, "Failed execution did not allocate its transient texture");
    auto& immediatelyReused = pool.acquire(
        {{32, 32}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment});
    require(&immediatelyReused == failedTexture, "Failed pre-submit execution did not release its transient texture");
}

void importedStateCommitContracts() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    auto texture = device.createTexture(
        {{8, 8}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment});
    State tracked = State::Undefined;
    Graph graph;
    const auto imported = graph.importTexture(*texture, tracked, "imported");
    graph.addPass("write", Graph::PassType::Graphics, {}, {}).write(imported, Graph::Usage::ColorWrite);
    aeg::tests::FakeCommands commands(&device.caps);
    graph.compile({imported});
    graph.execute(commands, device, pool, FrameToken{3});
    require(tracked == State::ColorAttachment, "Imported tracked state was not committed after execution");
    const auto recompiled = graph.compile({imported});
    require(!recompiled.passes.empty() &&
                (recompiled.passes[0].preBarriers.empty() ||
                 recompiled.passes[0].preBarriers[0].before == State::ColorAttachment),
            "Recompile did not seed imported state from its tracked state");

    State failingTracked = State::Undefined;
    Graph failing;
    const auto failingImported = failing.importTexture(*texture, failingTracked, "failing");
    failing.addPass("throws", Graph::PassType::Graphics, {}, [] { throw std::runtime_error("record failure"); })
        .write(failingImported, Graph::Usage::ColorWrite);
    aeg::tests::FakeCommands failingCommands(&device.caps);
    try {
        failing.compile({failingImported});
        failing.execute(failingCommands, device, pool, FrameToken{4});
    } catch (const std::runtime_error&) {
    }
    require(failingTracked == State::Undefined, "Imported tracked state changed after failed execution");
}

void externalSwapchainExecutionContract() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    ContractSwapchain swapchain;
    Graph graph;
    const auto image = graph.importSwapchain(swapchain, "swapchain");
    bool callbackRan = false;
    graph.addPass("ui", Graph::PassType::Graphics, {}, [&](Graph::PassContext& context) {
        callbackRan = &context.texture(image) == &swapchain.imageTexture && swapchain.commands.inRenderPass();
    }).write(image, Graph::Usage::ColorWrite);
    graph.compile({image});
    graph.execute(swapchain.commands, device, pool, FrameToken{1});
    require(callbackRan, "Imported swapchain pass did not reach its callback");
    require(!swapchain.commands.recordedPasses.empty() && swapchain.commands.recordedPasses.back().clears.size() == 1,
            "External swapchain pass did not receive its clear value");
}

void repeatedExecutionContract() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    Graph graph;
    const auto image = graph.createTexture(
        {{8, 8}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment}, "persistent", false);
    graph.addPass("draw", Graph::PassType::Graphics, {}, {}).write(image, Graph::Usage::ColorWrite);
    graph.compile({image});
    aeg::tests::FakeCommands first(&device.caps);
    graph.execute(first, device, pool, FrameToken{1});
    aeg::tests::FakeCommands second(&device.caps);
    rejectsAny([&] { graph.execute(second, device, pool, FrameToken{2}); },
               "RenderGraph allowed execution twice without recompilation");

    Graph mutated;
    const auto mutatedImage = mutated.createTexture(
        {{8, 8}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment}, "mutated", false);
    mutated.addPass("initial", Graph::PassType::Graphics, {}, {}).write(mutatedImage, Graph::Usage::ColorWrite);
    mutated.compile({mutatedImage});
    mutated.addPass("late", Graph::PassType::Graphics, {}, {}).write(mutatedImage, Graph::Usage::ColorWrite);
    aeg::tests::FakeCommands mutatedCommands(&device.caps);
    rejectsAny([&] { mutated.execute(mutatedCommands, device, pool, FrameToken{3}); },
               "RenderGraph executed a stale plan after mutation");
}

void culledTransientAllocationContract() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    auto importedTexture = device.createTexture(
        {{8, 8}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment});
    State tracked = State::Undefined;
    Graph graph;
    const auto output = graph.importTexture(*importedTexture, tracked, "output");
    const auto dead = graph.createTexture(
        {{8, 8}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment}, "dead", true);
    graph.addPass("output", Graph::PassType::Graphics, {}, {}).write(output, Graph::Usage::ColorWrite);
    graph.addPass("dead", Graph::PassType::Graphics, {}, {}).write(dead, Graph::Usage::ColorWrite);
    const auto result = graph.compile({output});
    require(result.resources[dead.index].firstUse == std::numeric_limits<std::size_t>::max(),
            "Culled transient resource unexpectedly has a first use");
    device.failTexture = true;
    aeg::tests::FakeCommands commands(&device.caps);
    graph.execute(commands, device, pool, FrameToken{1});
}
}

int main() {
    try {
        usageAndBarriers();
        orderingCullingAndDump();
        validationContracts();
        generationContract();
        transientPoolContracts();
        executionContracts();
        importedStateCommitContracts();
        externalSwapchainExecutionContract();
        repeatedExecutionContract();
        culledTransientAllocationContract();
        std::cout << "PASS RenderGraph contracts\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL RenderGraph contracts: " << error.what() << '\n';
        return 1;
    }
}
