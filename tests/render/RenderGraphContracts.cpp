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

void warDependencyContract() {
    aeg::tests::FakeDevice device;
    Graph graph;
    auto texture = device.createTexture({{8, 8}, aeg::rhi::Format::Rgba8Unorm,
                                         aeg::rhi::TextureUsage::ColorAttachment | aeg::rhi::TextureUsage::Sampled});
    State tracked = State::Sampled;
    const auto image = graph.importTexture(*texture, tracked, "war");
    graph.addPass("reader", Graph::PassType::Graphics, Graph::PassFlags::NeverCull, {})
        .read(image, Graph::Usage::SampledRead);
    graph.addPass("writer", Graph::PassType::Graphics, {}, {}).write(image, Graph::Usage::ColorWrite);
    const auto result = graph.compile({image});
    require(result.passes.size() == 2 && result.passes[0].name == "reader" && result.passes[1].name == "writer",
            "Missing WAR dependency allowed a writer to execute before its reader");
}

void clearAndTransientStateContracts() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    Graph clear;
    const auto image = clear.createTexture({{8, 8}, aeg::rhi::Format::Rgba8Unorm,
                                            aeg::rhi::TextureUsage::ColorAttachment},
                                           "clear", true);
    const aeg::rhi::ClearValue value{{0.25f, 0.5f, 0.75f, 1.f}};
    clear.addPass("clear", Graph::PassType::Graphics, {}, {}).write(image, Graph::Usage::ColorWrite).clear(value);
    aeg::tests::FakeCommands commands(&device.caps);
    clear.compile({image});
    clear.execute(commands, device, pool, FrameToken{1});
    require(commands.recordedPasses.size() == 1 &&
                commands.recordedPasses[0].description.attachments[0].load == aeg::rhi::LoadOp::Clear &&
                commands.recordedPasses[0].clears.size() == 1 && commands.recordedPasses[0].clears[0].color == value.color,
            "Graphics clear value was discarded by the generated render pass");

    Graph transient;
    const auto transientImage = transient.createTexture(
        {{8, 8}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment}, "transient-state", true);
    transient.addPass("write", Graph::PassType::Graphics, {}, {}).write(transientImage, Graph::Usage::ColorWrite);
    transient.compile({transientImage});
    aeg::tests::FakeCommands first(&device.caps);
    transient.execute(first, device, pool, FrameToken{2});
    pool.reclaimCompletedFrame(FrameToken{3});
    const auto recompiled = transient.compile({transientImage});
    require(!recompiled.passes.empty() && recompiled.passes[0].preBarriers.size() == 1 &&
                recompiled.passes[0].preBarriers[0].before == State::Undefined,
            "Transient recompile reused a stale physical resource state");
}

void persistentAttachmentLoadContract() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    auto texture = device.createTexture({{8, 8}, aeg::rhi::Format::Rgba8Unorm,
                                         aeg::rhi::TextureUsage::ColorAttachment | aeg::rhi::TextureUsage::Sampled});
    State tracked = State::Sampled;
    Graph graph;
    const auto image = graph.importTexture(*texture, tracked, "persistent-load");
    graph.addPass("blend", Graph::PassType::Graphics, {}, {}).write(image, Graph::Usage::ColorWrite);
    graph.compile({image});
    aeg::tests::FakeCommands commands(&device.caps);
    graph.execute(commands, device, pool, FrameToken{1});
    require(commands.recordedPasses.size() == 1 &&
                commands.recordedPasses.front().description.attachments.front().load == aeg::rhi::LoadOp::Load,
            "A defined first-use color attachment was discarded instead of loaded");
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

void presentAndFinalStateContracts() {
    ContractSwapchain swapchain;
    aeg::tests::FakeDevice device;
    Pool pool(device);
    Graph graph;
    const auto image = graph.importSwapchain(swapchain, "present");
    graph.addPass("clear", Graph::PassType::Graphics, {}, {}).write(image, Graph::Usage::ColorWrite);
    graph.addPass("present", Graph::PassType::Graphics, {}, {}).write(image, Graph::Usage::Present);
    graph.compile({image});
    aeg::tests::FakeCommands commands(&device.caps);
    graph.execute(commands, device, pool, FrameToken{1});
    require(commands.recordedPasses.size() == 1 && commands.transitions.size() == 2 &&
                commands.transitions.back().after == State::Present,
            "Present usage did not execute as a state-only final pass");

    rejects([&] {
        Graph invalid;
        const auto resource = invalid.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm,
                                                     aeg::rhi::TextureUsage::ColorAttachment},
                                                    "invalid-final", false);
        invalid.addPass("write", Graph::PassType::Graphics, {}, {})
            .write(resource, Graph::Usage::ColorWrite)
            .setFinalState(resource, State::Undefined);
        invalid.compile({resource});
    }, "Undefined final state was accepted");

    rejects([&] {
        Graph invalid;
        const auto resource = invalid.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm,
                                                     aeg::rhi::TextureUsage::ColorAttachment},
                                                    "incompatible-final", false);
        invalid.addPass("write", Graph::PassType::Graphics, {}, {})
            .write(resource, Graph::Usage::ColorWrite)
            .setFinalState(resource, State::Storage);
        invalid.compile({resource});
    }, "Incompatible final state was accepted");

    rejects([&] {
        Graph invalid;
        const auto resource = invalid.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm,
                                                     aeg::rhi::TextureUsage::ColorAttachment |
                                                         aeg::rhi::TextureUsage::Sampled},
                                                    "undeclared-final", true);
        invalid.addPass("producer", Graph::PassType::Graphics, {}, {}).write(resource, Graph::Usage::ColorWrite);
        invalid.addPass("final", Graph::PassType::Compute, Graph::PassFlags::NeverCull, {})
            .setFinalState(resource, State::Sampled);
        invalid.compile({});
    }, "Final state on an undeclared resource was accepted");
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

void ownedTextureResetLifetimeContract() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    Graph graph;
    const auto texture = graph.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm,
                                              aeg::rhi::TextureUsage::CopyDestination},
                                             "owned", false);
    graph.addPass("write", Graph::PassType::Copy, {}, {}).write(texture, Graph::Usage::CopyDst);
    graph.compile({texture});
    aeg::tests::FakeCommands commands(&device.caps);
    graph.execute(commands, device, pool, FrameToken{1});
    require(device.deferredCallbacks.empty(), "Owned texture was deferred before graph reset");
    graph.reset();
    require(device.deferredCallbacks.size() == 1,
            "Reset destroyed an executed graph-owned texture before GPU completion");
    device.waitIdle();
    require(device.deferredCallbacks.empty(), "Deferred graph-owned texture was not released at idle");
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

    Pool slots(device);
    slots.beginFrame(FrameToken{1}, 0, 2);
    auto& slotZero = slots.acquire(desc);
    slots.retire(slotZero, FrameToken{1});
    slots.beginFrame(FrameToken{2}, 1, 2);
    auto& slotOne = slots.acquire(desc);
    require(&slotOne != &slotZero, "Slot-one recording reused a slot-zero transient before completion");
    slots.retire(slotOne, FrameToken{2});
    slots.beginFrame(FrameToken{3}, 0, 2);
    auto& completedZero = slots.acquire(desc);
    require(&completedZero == &slotZero, "Completed slot did not reclaim its own transient");
    slots.retire(completedZero, FrameToken{3});
    slots.beginFrame(FrameToken{4}, 1, 2);
    auto& completedOne = slots.acquire(desc);
    require(&completedOne == &slotOne, "Out-of-order slot completion reclaimed the wrong transient");
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
    require(device.deferredCallbacks.empty(), "Graph execution appended an idle callback for frame-owned objects");
    require(pool.retiredRenderObjectCount() == 1,
            "Graph-owned render pass/framebuffer were not retired by frame token");
    pool.reclaimCompletedFrame(FrameToken{1});
    require(pool.retiredRenderObjectCount() == 1,
            "Graph-owned render pass/framebuffer reclaimed at the completing token");
    pool.reclaimCompletedFrame(FrameToken{2});
    require(pool.retiredRenderObjectCount() == 0,
            "Graph-owned render pass/framebuffer were not reclaimed after completion advanced");
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

void externalFramebufferRetirementContract() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    auto pass = device.createRenderPass({{{aeg::rhi::Format::Rgba8Unorm, 1, aeg::rhi::LoadOp::Discard,
                                           aeg::rhi::StoreOp::Store, State::ColorAttachment,
                                           State::ColorAttachment}},
                                         {0}, std::nullopt, {}});
    std::unique_ptr<aeg::rhi::Framebuffer> storage;
    Graph graph;
    const auto image = graph.createTexture(
        {{8, 8}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment}, "external", false);
    graph.addPass("external", Graph::PassType::Graphics, {}, {})
        .write(image, Graph::Usage::ColorWrite)
        .setExternalRenderPass(*pass, {8, 8}, &storage);
    graph.compile({image});
    aeg::tests::FakeCommands commands(&device.caps);
    graph.execute(commands, device, pool, FrameToken{7});
    require(storage != nullptr, "External framebuffer storage was not populated");
    require(pool.retiredRenderObjectCount() == 0,
            "Current external framebuffer was retired before its slot was replaced");

    graph.compile({image});
    graph.execute(commands, device, pool, FrameToken{8});
    require(pool.retiredRenderObjectCount() == 1,
            "Replacing external framebuffer storage did not retire the old object");
    pool.reclaimCompletedFrame(FrameToken{8});
    require(pool.retiredRenderObjectCount() == 1,
            "External framebuffer was reclaimed at its completing token");
    pool.reclaimCompletedFrame(FrameToken{9});
    require(pool.retiredRenderObjectCount() == 0,
            "External framebuffer was not reclaimed after completion advanced");
}

void externalFramebufferPairOwnershipContract() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    auto texture = device.createTexture({{8, 8}, aeg::rhi::Format::Rgba8Unorm,
                                         aeg::rhi::TextureUsage::ColorAttachment | aeg::rhi::TextureUsage::Sampled});
    auto pass = device.createRenderPass({{{aeg::rhi::Format::Rgba8Unorm, 1, aeg::rhi::LoadOp::Discard,
                                           aeg::rhi::StoreOp::Store, State::ColorAttachment,
                                           State::ColorAttachment}},
                                         {0}, std::nullopt, {}});
    auto framebuffer = device.createFramebuffer({pass.get(), {texture.get()}, {8, 8}});
    State tracked = State::Undefined;
    Graph graph;
    const auto image = graph.importTexture(*texture, tracked, "external-pair");
    bool callbackInsidePass = false;
    graph.addPass("external pair", Graph::PassType::Graphics, {}, [&](Graph::PassContext&) {
        callbackInsidePass = true;
    }).write(image, Graph::Usage::ColorWrite)
        .setFinalState(image, State::Sampled)
        .setExternalRenderPass(*pass, *framebuffer, {8, 8});
    graph.compile({image});
    aeg::tests::FakeCommands commands(&device.caps);
    graph.execute(commands, device, pool, FrameToken{11});
    require(callbackInsidePass && !commands.recordedPasses.empty(),
            "External pair callback did not execute inside a graph-owned render pass");
    require(commands.recordedPasses.front().framebuffer.pass == pass.get(),
            "External pair did not use the supplied render pass");
    require(commands.lastFramebuffer == framebuffer.get(),
            "External pair did not use the supplied framebuffer");
    require(pool.retiredRenderObjectCount() == 0,
            "Graph retired a caller-owned external framebuffer");
    require(tracked == State::Sampled, "External pair final state was not committed through graph barriers");
}

void externalFramebufferFailurePreservesStorageContract() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    auto texture = device.createTexture({{8, 8}, aeg::rhi::Format::Rgba8Unorm,
                                         aeg::rhi::TextureUsage::ColorAttachment | aeg::rhi::TextureUsage::Sampled});
    auto pass = device.createRenderPass({{{aeg::rhi::Format::Rgba8Unorm, 1, aeg::rhi::LoadOp::Discard,
                                           aeg::rhi::StoreOp::Store, State::ColorAttachment,
                                           State::ColorAttachment}},
                                         {0}, std::nullopt, {}});
    auto storage = device.createFramebuffer({pass.get(), {texture.get()}, {8, 8}});
    State tracked = State::Undefined;
    Graph graph;
    const auto image = graph.importTexture(*texture, tracked, "external-failure");
    graph.addPass("external", Graph::PassType::Graphics, {}, {})
        .write(image, Graph::Usage::ColorWrite)
        .setExternalRenderPass(*pass, {8, 8}, &storage);
    graph.compile({image});
    device.failFramebuffer = true;
    aeg::tests::FakeCommands commands(&device.caps);
    rejectsAny([&] { graph.execute(commands, device, pool, FrameToken{1}); },
               "Injected external framebuffer allocation failure was not propagated");
    require(storage != nullptr, "External framebuffer storage was consumed after replacement creation failed");
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

void computeStorageAndCullingContracts() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    aeg::tests::FakeCommands commands(&device.caps);
    Graph graph;
    const auto storage = graph.createTexture(
        {{8, 8}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::Storage}, "compute", true);
    bool firstInPass = true;
    bool secondInPass = true;
    graph.addPass("compute write", Graph::PassType::Compute, {}, [&](Graph::PassContext& context) {
        firstInPass = commands.inRenderPass();
        require(&context.texture(storage) != nullptr, "Compute callback could not resolve storage texture");
    }).write(storage, Graph::Usage::StorageWrite);
    graph.addPass("compute read modify", Graph::PassType::Compute, {}, [&](Graph::PassContext&) {
        secondInPass = commands.inRenderPass();
    }).readWrite(storage);
    const auto result = graph.compile({storage});
    require(result.passes.size() == 2, "Compute storage passes were not retained");
    require(result.passes[0].preBarriers.size() == 1 && result.passes[0].preBarriers[0].after == State::Storage,
            "Compute storage write did not enter GENERAL storage state");
    require(result.passes[1].preBarriers.size() == 1 && result.passes[1].preBarriers[0].before == State::Storage &&
                result.passes[1].preBarriers[0].after == State::Storage,
            "Storage write to read-modify-write did not emit a memory barrier");
    graph.execute(commands, device, pool, FrameToken{1});
    require(!firstInPass && !secondInPass, "Compute callback ran inside a graphics render pass");

    Graph invalid;
    const auto invalidStorage = invalid.createTexture(
        {{4, 4}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::Storage}, "invalid", false);
    invalid.addPass("graphics storage", Graph::PassType::Graphics, {}, {}).write(invalidStorage, Graph::Usage::StorageWrite);
    rejects([&] { invalid.compile({invalidStorage}); }, "Storage usage was accepted by a graphics pass");

    Graph invalidGraphics;
    const auto color = invalidGraphics.createTexture(
        {{4, 4}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment}, "color", false);
    invalidGraphics.addPass("compute color", Graph::PassType::Compute, {}, {}).write(color, Graph::Usage::ColorWrite);
    rejects([&] { invalidGraphics.compile({color}); }, "Color attachment usage was accepted by a compute pass");
    Graph invalidCopy;
    const auto copyTarget = invalidCopy.createTexture(
        {{4, 4}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::CopyDestination}, "copy", false);
    invalidCopy.addPass("graphics copy", Graph::PassType::Graphics, {}, {}).write(copyTarget, Graph::Usage::CopyDst);
    rejects([&] { invalidCopy.compile({copyTarget}); }, "Copy usage was accepted by a graphics pass");

    Graph roots;
    const auto dead = roots.createTexture(
        {{4, 4}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment}, "dead", true);
    const auto retained = roots.createTexture(
        {{4, 4}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::ColorAttachment}, "retained", true);
    roots.addPass("dead pass", Graph::PassType::Graphics, {}, {}).write(dead, Graph::Usage::ColorWrite);
    roots.addPass("never cull", Graph::PassType::Graphics, Graph::PassFlags::NeverCull, {})
        .write(retained, Graph::Usage::ColorWrite);
    const auto rootsResult = roots.compile({});
    require(rootsResult.passes.size() == 1 && rootsResult.passes.front().name == "never cull",
            "No-output compilation did not cull dead passes while retaining NeverCull");
    require(rootsResult.resources[dead.index].firstUse == std::numeric_limits<std::size_t>::max() &&
                rootsResult.resources[retained.index].firstUse == 0,
            "Culled/root transient lifetime metadata is incorrect");
}

void debugOrderingAndFrameReclamationContracts() {
    Graph graph;
    const auto first = graph.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm,
                                            aeg::rhi::TextureUsage::ColorAttachment}, "first", false);
    const auto second = graph.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm,
                                             aeg::rhi::TextureUsage::ColorAttachment}, "second", false);
    graph.addPass("first", Graph::PassType::Graphics, {}, {}).write(first, Graph::Usage::ColorWrite);
    graph.addPass("second", Graph::PassType::Graphics, {}, {}).write(second, Graph::Usage::ColorWrite);
    graph.compile({first, second});
    const auto dump = graph.debugDump();
    const auto firstPosition = dump.find("[0] first");
    const auto secondPosition = dump.find("[1] second");
    require(firstPosition != std::string::npos && secondPosition != std::string::npos && firstPosition < secondPosition,
            "Graph debug dump pass ordering is not deterministic");

    aeg::tests::FakeDevice device;
    Pool pool(device);
    const aeg::rhi::TextureDesc desc{{8, 8}, aeg::rhi::Format::Rgba8Unorm,
                                     aeg::rhi::TextureUsage::ColorAttachment};
    auto& frameOne = pool.acquire(desc);
    pool.retire(frameOne, FrameToken{10});
    pool.reclaimCompletedFrame(FrameToken{10});
    auto& frameTwo = pool.acquire(desc);
    require(&frameTwo != &frameOne, "Frame-token reclamation reused a texture before GPU completion");
    pool.retire(frameTwo, FrameToken{11});
    pool.reclaimCompletedFrame(FrameToken{12});
    auto& rebuilt = pool.acquire(desc);
    require(&rebuilt == &frameOne || &rebuilt == &frameTwo,
            "Resize/rebuild frame-token reclamation did not release completed textures");

    aeg::tests::FakeDevice importedDevice;
    auto importedTexture = importedDevice.createTexture(
        {{8, 8}, aeg::rhi::Format::Rgba8Unorm, aeg::rhi::TextureUsage::Storage});
    State importedState = State::Storage;
    Graph importedGraph;
    const auto imported = importedGraph.importTexture(*importedTexture, importedState, "imported storage");
    importedGraph.addPass("first storage read", Graph::PassType::Compute, {}, {})
        .read(imported, Graph::Usage::StorageRead);
    const auto importedResult = importedGraph.compile({imported});
    require(importedResult.passes.size() == 1 && importedResult.passes[0].preBarriers.size() == 1 &&
                importedResult.passes[0].preBarriers[0].before == State::Storage &&
                importedResult.passes[0].preBarriers[0].after == State::Storage,
            "First use of imported storage did not emit a Storage-to-Storage dependency barrier");
}

void diagnosticScopeContracts() {
    aeg::tests::FakeDevice device;
    Pool pool(device);
    Graph graph;
    const auto target = graph.createTexture({{4, 4}, aeg::rhi::Format::Rgba8Unorm,
                                             aeg::rhi::TextureUsage::ColorAttachment},
                                            "diagnostic-target", false);
    graph.addPass("diagnostic pass", Graph::PassType::Graphics, {}, {}).write(target, Graph::Usage::ColorWrite);
    graph.compile({target});
    aeg::tests::FakeCommands commands(&device.caps);
    graph.execute(commands, device, pool, FrameToken{1});
    require(commands.debugLabels.size() == 1 && commands.debugLabels.front() == "diagnostic pass" &&
                commands.gpuPasses.size() == 1 && commands.gpuPasses.front() == "diagnostic pass",
            "RenderGraph did not emit matching debug label and GPU timing scopes");
}
}

int main() {
    try {
        usageAndBarriers();
        warDependencyContract();
        clearAndTransientStateContracts();
        persistentAttachmentLoadContract();
        orderingCullingAndDump();
        validationContracts();
        presentAndFinalStateContracts();
        generationContract();
        ownedTextureResetLifetimeContract();
        transientPoolContracts();
        executionContracts();
        externalFramebufferRetirementContract();
        externalFramebufferPairOwnershipContract();
        externalFramebufferFailurePreservesStorageContract();
        importedStateCommitContracts();
        externalSwapchainExecutionContract();
        repeatedExecutionContract();
        culledTransientAllocationContract();
        computeStorageAndCullingContracts();
        debugOrderingAndFrameReclamationContracts();
        diagnosticScopeContracts();
        std::cout << "PASS RenderGraph contracts\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL RenderGraph contracts: " << error.what() << '\n';
        return 1;
    }
}
