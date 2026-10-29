#include <RenderGraph.hpp>
#include "../rhi/fake/FakeDevice.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using Graph = aeg::render::RenderGraph;
using State = aeg::rhi::ResourceState;
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
}

int main() {
    try {
        usageAndBarriers();
        orderingCullingAndDump();
        validationContracts();
        generationContract();
        std::cout << "PASS RenderGraph contracts\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL RenderGraph contracts: " << error.what() << '\n';
        return 1;
    }
}
