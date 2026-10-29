#include <RenderGraph.hpp>
#include "TransientTexturePool.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace aeg::render {
namespace {
constexpr auto invalidIndex = std::numeric_limits<std::size_t>::max();

const char* stateName(rhi::ResourceState state) {
    switch (state) {
    case rhi::ResourceState::Undefined: return "Undefined";
    case rhi::ResourceState::Sampled: return "Sampled";
    case rhi::ResourceState::ColorAttachment: return "ColorAttachment";
    case rhi::ResourceState::DepthAttachment: return "DepthAttachment";
    case rhi::ResourceState::CopySource: return "CopySource";
    case rhi::ResourceState::CopyDestination: return "CopyDestination";
    case rhi::ResourceState::Present: return "Present";
    case rhi::ResourceState::Storage: return "Storage";
    }
    return "Unknown";
}
const char* formatName(rhi::Format format) {
    switch (format) {
    case rhi::Format::Undefined: return "Undefined";
    case rhi::Format::Rgba8Unorm: return "Rgba8Unorm";
    case rhi::Format::Rgba8Srgb: return "Rgba8Srgb";
    case rhi::Format::Bgra8Unorm: return "Bgra8Unorm";
    case rhi::Format::Bgra8Srgb: return "Bgra8Srgb";
    case rhi::Format::Rgba16Float: return "Rgba16Float";
    case rhi::Format::Rgba32Float: return "Rgba32Float";
    case rhi::Format::R32Float: return "R32Float";
    case rhi::Format::D32Float: return "D32Float";
    case rhi::Format::D24UnormS8: return "D24UnormS8";
    case rhi::Format::D16Unorm: return "D16Unorm";
    }
    return "Unknown";
}
const char* usageName(RenderGraph::Usage usage) {
    switch (usage) {
    case RenderGraph::Usage::SampledRead: return "SampledRead";
    case RenderGraph::Usage::ColorWrite: return "ColorWrite";
    case RenderGraph::Usage::DepthWrite: return "DepthWrite";
    case RenderGraph::Usage::StorageRead: return "StorageRead";
    case RenderGraph::Usage::StorageWrite: return "StorageWrite";
    case RenderGraph::Usage::StorageReadWrite: return "StorageReadWrite";
    case RenderGraph::Usage::CopySrc: return "CopySrc";
    case RenderGraph::Usage::CopyDst: return "CopyDst";
    case RenderGraph::Usage::Present: return "Present";
    }
    return "Unknown";
}
const char* passTypeName(RenderGraph::PassType type) {
    switch (type) {
    case RenderGraph::PassType::Graphics: return "Graphics";
    case RenderGraph::PassType::Compute: return "Compute";
    case RenderGraph::PassType::Copy: return "Copy";
    }
    return "Unknown";
}
rhi::ResourceState usageState(RenderGraph::Usage usage) {
    switch (usage) {
    case RenderGraph::Usage::SampledRead: return rhi::ResourceState::Sampled;
    case RenderGraph::Usage::ColorWrite: return rhi::ResourceState::ColorAttachment;
    case RenderGraph::Usage::DepthWrite: return rhi::ResourceState::DepthAttachment;
    case RenderGraph::Usage::StorageRead:
    case RenderGraph::Usage::StorageWrite:
    case RenderGraph::Usage::StorageReadWrite: return rhi::ResourceState::Storage;
    case RenderGraph::Usage::CopySrc: return rhi::ResourceState::CopySource;
    case RenderGraph::Usage::CopyDst: return rhi::ResourceState::CopyDestination;
    case RenderGraph::Usage::Present: return rhi::ResourceState::Present;
    }
    return rhi::ResourceState::Undefined;
}
bool usageReads(RenderGraph::Usage usage) {
    return usage == RenderGraph::Usage::SampledRead || usage == RenderGraph::Usage::StorageRead ||
           usage == RenderGraph::Usage::CopySrc || usage == RenderGraph::Usage::StorageReadWrite;
}
bool usageWrites(RenderGraph::Usage usage) {
    return usage == RenderGraph::Usage::ColorWrite || usage == RenderGraph::Usage::DepthWrite ||
           usage == RenderGraph::Usage::StorageWrite || usage == RenderGraph::Usage::CopyDst ||
           usage == RenderGraph::Usage::Present || usage == RenderGraph::Usage::StorageReadWrite;
}
bool hasUsage(rhi::TextureUsage flags, rhi::TextureUsage required) {
    return rhi::contains(flags, required);
}
}

struct RenderGraph::Impl {
    struct Resource {
        Handle handle;
        TextureDesc description;
        std::string name;
        bool imported{};
        bool transient{};
        bool swapchain{};
        rhi::Texture* texture{};
        rhi::Swapchain* swapchainOwner{};
        rhi::ResourceState* trackedState{};
        rhi::ResourceState initialState{rhi::ResourceState::Undefined};
        std::optional<rhi::ResourceState> finalState;
        std::unique_ptr<rhi::Texture> owned;
    };
    struct Declaration {
        Handle resource;
        Usage usage{};
        bool read{};
        bool write{};
    };
    struct Pass {
        std::string name;
        PassType type{PassType::Graphics};
        PassFlags flags{PassFlags::None};
        ExecuteCallback callback;
        std::vector<Declaration> declarations;
        std::vector<rhi::ClearValue> clearValues;
        rhi::RenderPass* externalPass{};
        std::optional<rhi::Extent2D> externalExtent;
        std::unique_ptr<rhi::Framebuffer>* externalFramebufferStorage{};
    };

    std::uint32_t generation{1};
    std::vector<Resource> resources;
    std::vector<Pass> passes;
    CompileResult lastResult;
    std::string dump;
    bool compiled{};
    bool executed{};

    Resource& resource(Handle handle) {
        if (!handle || handle.generation != generation || handle.index >= resources.size())
            throw std::invalid_argument("RenderGraph handle has expired or is invalid");
        return resources[handle.index];
    }
    const Resource& resource(Handle handle) const {
        if (!handle || handle.generation != generation || handle.index >= resources.size())
            throw std::invalid_argument("RenderGraph handle has expired or is invalid");
        return resources[handle.index];
    }
    void validateUsage(const Resource& resource, const Pass& pass, const Declaration& declaration) const {
        const auto usage = declaration.usage;
        const auto& desc = resource.description;
        const auto fail = [&](const char* reason) {
            throw std::invalid_argument("RenderGraph pass '" + pass.name + "' resource '" + resource.name + "': " + reason);
        };
        if (usage == Usage::Present) {
            if (!resource.swapchain) fail("Present usage requires an imported swapchain resource");
            if (pass.type != PassType::Graphics) fail("Present usage requires a graphics pass");
            return;
        }
        if ((usage == Usage::ColorWrite || usage == Usage::DepthWrite) && pass.type != PassType::Graphics)
            fail("Attachment writes require a graphics pass");
        if ((usage == Usage::CopySrc || usage == Usage::CopyDst) && pass.type != PassType::Copy)
            fail("Copy usage requires a copy pass");
        if (usage == Usage::SampledRead && pass.type == PassType::Copy)
            fail("Sampled reads require a graphics or compute pass");
        if (usage == Usage::SampledRead && !hasUsage(desc.usage, rhi::TextureUsage::Sampled))
            fail("SampledRead requires Sampled texture usage");
        if (usage == Usage::ColorWrite && !hasUsage(desc.usage, rhi::TextureUsage::ColorAttachment))
            fail("ColorWrite requires ColorAttachment texture usage");
        if (usage == Usage::DepthWrite && !hasUsage(desc.usage, rhi::TextureUsage::DepthAttachment))
            fail("DepthWrite requires DepthAttachment texture usage");
        if (usage == Usage::StorageRead || usage == Usage::StorageWrite || usage == Usage::StorageReadWrite) {
            if (pass.type != PassType::Compute) fail("Storage usage requires a compute pass");
            if (!hasUsage(desc.usage, rhi::TextureUsage::Storage))
                fail("Storage usage requires Storage texture usage");
        }
        if (usage == Usage::CopySrc && !hasUsage(desc.usage, rhi::TextureUsage::CopySource))
            fail("CopySrc requires CopySource texture usage");
        if (usage == Usage::CopyDst && !hasUsage(desc.usage, rhi::TextureUsage::CopyDestination))
            fail("CopyDst requires CopyDestination texture usage");
    }
};

RenderGraph::RenderGraph() : impl_(std::make_unique<Impl>()) {}
RenderGraph::~RenderGraph() = default;
RenderGraph::RenderGraph(RenderGraph&&) noexcept = default;
RenderGraph& RenderGraph::operator=(RenderGraph&&) noexcept = default;

RenderGraph::Handle RenderGraph::createTexture(const rhi::TextureDesc& description, std::string name, bool transient) {
    rhi::validate(description);
    const Handle handle{static_cast<std::uint32_t>(impl_->resources.size()), impl_->generation};
    impl_->resources.push_back({handle, description, std::move(name), false, transient, false, nullptr, nullptr, nullptr,
                                rhi::ResourceState::Undefined, std::nullopt});
    impl_->compiled = false;
    return handle;
}

RenderGraph::Handle RenderGraph::importTexture(rhi::Texture& texture, rhi::ResourceState& trackedState, std::string name) {
    rhi::validate(texture.description());
    const Handle handle{static_cast<std::uint32_t>(impl_->resources.size()), impl_->generation};
    impl_->resources.push_back({handle, texture.description(), std::move(name), true, false, false, &texture, nullptr,
                                &trackedState, trackedState, std::nullopt});
    impl_->compiled = false;
    return handle;
}

RenderGraph::Handle RenderGraph::importSwapchain(rhi::Swapchain& swapchain, std::string name) {
    auto& texture = swapchain.image();
    rhi::validate(texture.description());
    const Handle handle{static_cast<std::uint32_t>(impl_->resources.size()), impl_->generation};
    impl_->resources.push_back({handle, texture.description(), std::move(name), true, false, true, &texture, &swapchain,
                                nullptr, swapchain.imageState(), rhi::ResourceState::Present});
    impl_->compiled = false;
    return handle;
}

RenderGraph::PassBuilder RenderGraph::addPass(std::string name, PassType type, PassFlags flags,
                                               ExecuteCallback execute) {
    impl_->passes.push_back({std::move(name), type, flags, std::move(execute), {}, {}});
    impl_->compiled = false;
    return PassBuilder(this, impl_->passes.size() - 1);
}
RenderGraph::PassBuilder& RenderGraph::PassBuilder::read(Handle resource, Usage usage) {
    if (!graph_ || index_ >= graph_->impl_->passes.size()) throw std::logic_error("Invalid RenderGraph pass builder");
    graph_->impl_->passes[index_].declarations.push_back({resource, usage, true, false});
    graph_->impl_->compiled = false;
    return *this;
}
RenderGraph::PassBuilder& RenderGraph::PassBuilder::write(Handle resource, Usage usage) {
    if (!graph_ || index_ >= graph_->impl_->passes.size()) throw std::logic_error("Invalid RenderGraph pass builder");
    graph_->impl_->passes[index_].declarations.push_back({resource, usage, false, true});
    graph_->impl_->compiled = false;
    return *this;
}
RenderGraph::PassBuilder& RenderGraph::PassBuilder::readWrite(Handle resource, Usage usage) {
    if (!graph_ || index_ >= graph_->impl_->passes.size()) throw std::logic_error("Invalid RenderGraph pass builder");
    graph_->impl_->passes[index_].declarations.push_back({resource, usage, true, true});
    graph_->impl_->compiled = false;
    return *this;
}
RenderGraph::PassBuilder& RenderGraph::PassBuilder::clear(rhi::ClearValue value) {
    if (!graph_ || index_ >= graph_->impl_->passes.size()) throw std::logic_error("Invalid RenderGraph pass builder");
    auto& pass = graph_->impl_->passes[index_];
    if (pass.type != PassType::Graphics) throw std::invalid_argument("RenderGraph clear values require a graphics pass");
    pass.clearValues.push_back(value);
    graph_->impl_->compiled = false;
    return *this;
}

RenderGraph::PassBuilder& RenderGraph::PassBuilder::setExternalRenderPass(
    rhi::RenderPass& pass, rhi::Extent2D extent, std::unique_ptr<rhi::Framebuffer>* framebufferStorage) {
    if (!graph_ || index_ >= graph_->impl_->passes.size()) throw std::logic_error("Invalid RenderGraph pass builder");
    auto& source = graph_->impl_->passes[index_];
    if (source.type != PassType::Graphics) throw std::invalid_argument("External render passes require graphics passes");
    source.externalPass = &pass;
    source.externalExtent = extent;
    source.externalFramebufferStorage = framebufferStorage;
    graph_->impl_->compiled = false;
    return *this;
}

RenderGraph::CompileResult RenderGraph::compile(std::span<const Handle> outputs) {
    auto& impl = *impl_;
    impl.compiled = false;
    impl.executed = false;
    for (auto& resource : impl.resources) {
        if (resource.trackedState)
            resource.initialState = *resource.trackedState;
        else if (resource.swapchainOwner)
            resource.initialState = resource.swapchainOwner->imageState();
    }
    const auto passCount = impl.passes.size();
    const auto resourceCount = impl.resources.size();
    std::vector<std::vector<std::size_t>> outgoing(passCount), incoming(passCount);
    std::vector<std::vector<std::size_t>> writers(resourceCount);
    const auto addEdge = [&](std::size_t from, std::size_t to) {
        if (from == to) return;
        if (std::ranges::find(outgoing[from], to) == outgoing[from].end()) {
            outgoing[from].push_back(to);
            incoming[to].push_back(from);
        }
    };
    for (std::size_t passIndex = 0; passIndex < passCount; ++passIndex) {
        auto& pass = impl.passes[passIndex];
        std::vector<Handle> seen;
        for (const auto& declaration : pass.declarations) {
            auto& resource = impl.resource(declaration.resource);
            impl.validateUsage(resource, pass, declaration);
            if (declaration.read && !usageReads(declaration.usage))
                throw std::invalid_argument("RenderGraph read declaration uses a write-only usage");
            if (declaration.write && !usageWrites(declaration.usage))
                throw std::invalid_argument("RenderGraph write declaration uses a read-only usage");
            if (declaration.usage == Usage::StorageReadWrite &&
                (!declaration.read || !declaration.write || pass.type != PassType::Compute))
                throw std::invalid_argument("RenderGraph StorageReadWrite is only valid for compute passes");
            if (declaration.usage != Usage::StorageReadWrite &&
                (declaration.read && declaration.write))
                throw std::invalid_argument("RenderGraph pass has an invalid read/write declaration");
            if (std::ranges::find(seen, declaration.resource) != seen.end())
                throw std::invalid_argument("RenderGraph pass '" + pass.name + "' declares a resource more than once");
            seen.push_back(declaration.resource);
            if (declaration.write) writers[declaration.resource.index].push_back(passIndex);
        }
    }
    for (const auto& resourceWriters : writers)
        for (std::size_t i = 1; i < resourceWriters.size(); ++i) addEdge(resourceWriters[i - 1], resourceWriters[i]);

    for (std::size_t passIndex = 0; passIndex < passCount; ++passIndex) {
        for (const auto& declaration : impl.passes[passIndex].declarations) {
            if (!declaration.read || declaration.write) continue;
            const auto& resource = impl.resource(declaration.resource);
            const auto& resourceWriters = writers[declaration.resource.index];
            auto previous = std::find_if(resourceWriters.rbegin(), resourceWriters.rend(),
                                         [&](std::size_t writer) { return writer < passIndex; });
            if (previous != resourceWriters.rend())
                addEdge(*previous, passIndex);
            else if (!resource.imported && resourceWriters.empty())
                throw std::invalid_argument("RenderGraph pass '" + impl.passes[passIndex].name +
                                            "' reads undefined resource '" + resource.name + "'");
            else if (!resource.imported && previous == resourceWriters.rend())
                addEdge(resourceWriters.front(), passIndex);
        }
    }

    std::vector<bool> retained(passCount);
    std::queue<std::size_t> roots;
    for (const auto& pass : impl.passes)
        if (contains(pass.flags, PassFlags::NeverCull)) roots.push(static_cast<std::size_t>(&pass - impl.passes.data()));
    for (const auto handle : outputs) {
        const auto& resource = impl.resource(handle);
        (void)resource;
        for (std::size_t passIndex = passCount; passIndex-- > 0;)
            if (std::ranges::any_of(impl.passes[passIndex].declarations,
                                    [&](const auto& declaration) { return declaration.resource == handle; })) {
                roots.push(passIndex);
                break;
            }
    }
    for (std::size_t index = 0; index < resourceCount; ++index)
        if (impl.resources[index].swapchain)
            for (std::size_t passIndex = passCount; passIndex-- > 0;)
                if (std::ranges::any_of(impl.passes[passIndex].declarations,
                                        [&](const auto& declaration) {
                                            return declaration.resource.index == index;
                                        })) {
                    roots.push(passIndex);
                    break;
                }
    while (!roots.empty()) {
        const auto pass = roots.front();
        roots.pop();
        if (retained[pass]) continue;
        retained[pass] = true;
        for (const auto dependency : incoming[pass]) roots.push(dependency);
    }

    std::vector<std::size_t> indegree(passCount);
    for (std::size_t from = 0; from < passCount; ++from)
        if (retained[from])
            for (const auto to : outgoing[from])
                if (retained[to]) ++indegree[to];
    std::vector<std::size_t> ready;
    for (std::size_t index = 0; index < passCount; ++index)
        if (retained[index] && indegree[index] == 0) ready.push_back(index);
    std::vector<std::size_t> order;
    while (!ready.empty()) {
        const auto index = ready.front();
        ready.erase(ready.begin());
        order.push_back(index);
        for (const auto to : outgoing[index])
            if (retained[to] && --indegree[to] == 0) {
                const auto position = std::ranges::lower_bound(ready, to);
                ready.insert(position, to);
            }
    }
    const auto retainedCount = static_cast<std::size_t>(std::ranges::count(retained, true));
    if (order.size() != retainedCount) {
        std::ostringstream message;
        message << "RenderGraph contains a dependency cycle involving passes";
        bool first = true;
        for (std::size_t index = 0; index < passCount; ++index)
            if (retained[index] && indegree[index] != 0) {
                message << (first ? " '" : ", '") << impl.passes[index].name << "'";
                first = false;
            }
        throw std::invalid_argument(message.str());
    }

    // A read with no producer earlier in declaration order is undefined.  The
    // provisional edge above exists only so that genuine dependency cycles are
    // reported as cycles instead of being mistaken for an undefined read.
    for (std::size_t passIndex = 0; passIndex < passCount; ++passIndex)
        for (const auto& declaration : impl.passes[passIndex].declarations) {
            if (!declaration.read || declaration.write) continue;
            const auto& resource = impl.resource(declaration.resource);
            if (resource.imported) continue;
            const auto& resourceWriters = writers[declaration.resource.index];
            const auto producer = std::ranges::find_if(resourceWriters,
                                                        [&](std::size_t writer) { return writer < passIndex; });
            if (producer == resourceWriters.end())
                throw std::invalid_argument("RenderGraph pass '" + impl.passes[passIndex].name +
                                            "' reads resource '" + resource.name +
                                            "' before its writer");
        }

    CompileResult result;
    result.retainedPassOrder = order;
    result.passes.reserve(order.size());
    std::vector<std::size_t> compiledIndex(passCount, invalidIndex);
    for (std::size_t index = 0; index < order.size(); ++index) {
        const auto original = order[index];
        compiledIndex[original] = index;
        const auto& pass = impl.passes[original];
        result.passes.push_back({original, pass.name, pass.type, pass.flags, false, {}, {}, {}});
        for (const auto& declaration : pass.declarations)
            result.passes.back().usages.push_back({declaration.resource, declaration.usage, declaration.read,
                                                   declaration.write});
    }
    result.allPasses.reserve(passCount);
    for (std::size_t index = 0; index < passCount; ++index) {
        const auto& pass = impl.passes[index];
        result.allPasses.push_back({index, pass.name, pass.type, pass.flags, !retained[index], {}, {}, {}});
        for (const auto& declaration : pass.declarations)
            result.allPasses.back().usages.push_back({declaration.resource, declaration.usage, declaration.read,
                                                      declaration.write});
    }

    std::vector<std::size_t> firstUse(resourceCount, invalidIndex), lastUse(resourceCount, invalidIndex);
    std::vector<rhi::ResourceState> current;
    current.reserve(resourceCount);
    for (const auto& resource : impl.resources) current.push_back(resource.initialState);
    for (std::size_t compiled = 0; compiled < order.size(); ++compiled) {
        const auto original = order[compiled];
        auto& pass = result.passes[compiled];
        for (const auto& declaration : impl.passes[original].declarations) {
            const auto index = declaration.resource.index;
            firstUse[index] = std::min(firstUse[index], compiled);
            lastUse[index] = compiled;
            const auto desired = usageState(declaration.usage);
            // Storage resources remain in GENERAL layout across compute passes,
            // but a write/read-modify-write boundary still needs an explicit
            // memory dependency. RHI backends represent this as Storage ->
            // Storage so access scopes are refreshed without changing layout.
            if (current[index] != desired || desired == rhi::ResourceState::Storage) {
                pass.preBarriers.push_back({declaration.resource, current[index], desired, compiled});
                current[index] = desired;
            }
        }
    }
    for (std::size_t index = 0; index < resourceCount; ++index) {
        const auto& resource = impl.resources[index];
        if (lastUse[index] != invalidIndex && resource.finalState && current[index] != *resource.finalState) {
            result.passes[lastUse[index]].postBarriers.push_back(
                {resource.handle, current[index], *resource.finalState, lastUse[index]});
        }
        result.resources.push_back({resource.handle, resource.name, resource.description, resource.imported,
                                    resource.transient, resource.swapchain, firstUse[index], lastUse[index]});
    }
    impl.lastResult = result;
    std::ostringstream dump;
    dump << "RenderGraph\npasses:\n";
    const auto appendDumpPass = [&](const CompiledPass& pass) {
        dump << "  [" << pass.originalIndex << "] " << pass.name << " type=" << passTypeName(pass.type)
             << " flags=" << static_cast<std::uint32_t>(pass.flags) << " culled=" << (pass.culled ? "true" : "false")
             << "\n";
        for (const auto& use : pass.usages)
            dump << "    " << usageName(use.usage) << " resource=" << impl.resources[use.resource.index].name << "\n";
        if (!pass.culled) {
            const auto& compiled = result.passes[compiledIndex[pass.originalIndex]];
            for (const auto& barrier : compiled.preBarriers)
                dump << "    pre " << impl.resources[barrier.resource.index].name << " " << stateName(barrier.before)
                     << " -> " << stateName(barrier.after) << "\n";
            for (const auto& barrier : compiled.postBarriers)
                dump << "    post " << impl.resources[barrier.resource.index].name << " " << stateName(barrier.before)
                     << " -> " << stateName(barrier.after) << "\n";
        }
    };
    // Retained entries are emitted in their compiled/topological execution
    // order. Culled entries follow in declaration order so every pass remains
    // visible while the dump mirrors actual recording order for live work.
    for (const auto& pass : result.passes) appendDumpPass(pass);
    for (const auto& pass : result.allPasses)
        if (pass.culled) appendDumpPass(pass);
    dump << "resources:\n";
    for (const auto& resource : result.resources)
        dump << "  " << resource.name << " imported=" << (resource.imported ? "true" : "false")
             << " transient=" << (resource.transient ? "true" : "false") << " desc=" << resource.description.extent.width
             << "x" << resource.description.extent.height << "," << formatName(resource.description.format)
             << ",usage=" << static_cast<std::uint32_t>(resource.description.usage)
             << ",samples=" << resource.description.samples << ",mips=" << resource.description.mipLevels
             << ",dimension=" << static_cast<int>(resource.description.dimension) << " first=" << resource.firstUse
             << " last=" << resource.lastUse << "\n";
    impl.dump = dump.str();
    impl.compiled = true;
    impl.executed = false;
    return result;
}

void RenderGraph::execute(rhi::CommandList& commands, rhi::Device& device, TransientTexturePool& pool,
                          FrameToken frameToken) {
    auto& impl = *impl_;
    if (!impl.compiled)
        throw std::logic_error("RenderGraph must be compiled before execute");
    if (impl.executed)
        throw std::logic_error("RenderGraph was already executed; compile it again before reuse");

    std::vector<rhi::Texture*> physical(impl.resources.size());
    std::vector<rhi::Texture*> transient;
    try {
        for (std::size_t index = 0; index < impl.resources.size(); ++index) {
            auto& resource = impl.resources[index];
            if (resource.imported) {
                physical[index] = resource.texture;
            } else if (resource.transient) {
                if (index < impl.lastResult.resources.size() &&
                    impl.lastResult.resources[index].firstUse != invalidIndex) {
                    physical[index] = &pool.acquire(resource.description);
                    transient.push_back(physical[index]);
                }
            } else {
                if (!resource.owned) resource.owned = device.createTexture(resource.description);
                physical[index] = resource.owned.get();
            }
            resource.texture = physical[index];
        }

        const auto resolve = [&](Handle handle) -> rhi::Texture* {
            auto& resource = impl.resource(handle);
            return physical[resource.handle.index];
        };
        const auto emitBarrier = [&](const Barrier& barrier) {
            auto* texture = resolve(barrier.resource);
            if (!texture) throw std::logic_error("RenderGraph barrier references an unbound texture");
            commands.transition(*texture, barrier.before, barrier.after);
        };

        for (std::size_t passIndex = 0; passIndex < impl.lastResult.passes.size(); ++passIndex) {
            const auto& compiled = impl.lastResult.passes[passIndex];
            const auto& source = impl.passes[compiled.originalIndex];
            for (const auto& barrier : compiled.preBarriers) emitBarrier(barrier);

            PassContext context;
            context.resolver_ = resolve;
            if (compiled.type == PassType::Graphics) {
                std::vector<rhi::Texture*> attachments;
                std::vector<std::uint32_t> colors;
                std::optional<std::uint32_t> depth;
                rhi::Swapchain* externalSwapchain = nullptr;
                for (const auto& usage : compiled.usages) {
                    if (usage.usage != Usage::ColorWrite && usage.usage != Usage::DepthWrite) continue;
                    auto& resource = impl.resource(usage.resource);
                    if (resource.swapchain) externalSwapchain = resource.swapchainOwner;
                    const auto attachmentIndex = static_cast<std::uint32_t>(attachments.size());
                    attachments.push_back(resolve(usage.resource));
                    if (usage.usage == Usage::DepthWrite)
                        depth = attachmentIndex;
                    else
                        colors.push_back(attachmentIndex);
                }
                if (attachments.empty()) throw std::invalid_argument("Graphics RenderGraph pass has no attachments");

                std::unique_ptr<rhi::RenderPass> ownedPass;
                std::unique_ptr<rhi::Framebuffer> ownedFramebuffer;
                rhi::RenderPass* renderPass = nullptr;
                rhi::Framebuffer* framebuffer = nullptr;
                rhi::Extent2D extent = attachments.front()->description().extent;
                std::vector<rhi::ClearValue> clearValues = source.clearValues;
                std::unique_ptr<rhi::Framebuffer> externalFramebuffer;
                if (source.externalPass) {
                    renderPass = source.externalPass;
                    extent = source.externalExtent.value_or(extent);
                    if (source.externalFramebufferStorage) {
                        *source.externalFramebufferStorage = device.createFramebuffer({renderPass, attachments, extent});
                        framebuffer = source.externalFramebufferStorage->get();
                    } else {
                        externalFramebuffer = device.createFramebuffer({renderPass, attachments, extent});
                        framebuffer = externalFramebuffer.get();
                    }
                } else if (externalSwapchain) {
                    const bool loadCompatible = contains(compiled.flags, PassFlags::NeverCull);
                    renderPass = loadCompatible ? &externalSwapchain->loadRenderPass()
                                                : &externalSwapchain->renderPass();
                    framebuffer = loadCompatible ? &externalSwapchain->loadFramebuffer()
                                                 : &externalSwapchain->framebuffer();
                    extent = externalSwapchain->extent();
                    if (clearValues.empty()) clearValues.resize(attachments.size());
                } else {
                    rhi::RenderPassDesc description;
                    description.colors = colors;
                    description.depth = depth;
                    description.attachments.reserve(attachments.size());
                    for (const auto* attachment : attachments) {
                        const auto isDepth = rhi::isDepthFormat(attachment->description().format);
                        description.attachments.push_back({attachment->description().format,
                                                           attachment->description().samples,
                                                           rhi::LoadOp::Discard,
                                                           rhi::StoreOp::Store,
                                                           isDepth ? rhi::ResourceState::DepthAttachment
                                                                   : rhi::ResourceState::ColorAttachment,
                                                           isDepth ? rhi::ResourceState::DepthAttachment
                                                                   : rhi::ResourceState::ColorAttachment});
                    }
                    ownedPass = device.createRenderPass(description);
                    rhi::FramebufferDesc framebufferDescription{ownedPass.get(), attachments, extent};
                    ownedFramebuffer = device.createFramebuffer(framebufferDescription);
                    renderPass = ownedPass.get();
                    framebuffer = ownedFramebuffer.get();
                }

                bool active = false;
                try {
                    commands.beginRenderPass(*renderPass, *framebuffer, extent, clearValues);
                    active = true;
                    if (source.callback) source.callback(context);
                    commands.endRenderPass();
                    active = false;
                } catch (...) {
                    if (active) commands.endRenderPass();
                    throw;
                }
            } else if (source.callback) {
                source.callback(context);
            }
            for (const auto& barrier : compiled.postBarriers) emitBarrier(barrier);
        }

        for (auto& resource : impl.resources) {
            if (resource.trackedState || resource.owned || resource.transient) {
                auto current = resource.initialState;
                for (const auto& pass : impl.lastResult.passes) {
                    for (const auto& usage : pass.usages)
                        if (usage.resource == resource.handle) current = usageState(usage.usage);
                    for (const auto& barrier : pass.postBarriers)
                        if (barrier.resource == resource.handle) current = barrier.after;
                }
                resource.initialState = current;
                if (resource.trackedState) *resource.trackedState = current;
            }
        }
        for (auto* texture : transient) pool.retire(*texture, frameToken);
        impl.executed = true;
    } catch (...) {
        for (auto* texture : transient) pool.releaseImmediately(*texture);
        throw;
    }
}

void RenderGraph::reset() {
    impl_->resources.clear();
    impl_->passes.clear();
    impl_->lastResult = {};
    impl_->dump.clear();
    impl_->compiled = false;
    impl_->executed = false;
    if (++impl_->generation == 0) impl_->generation = 1;
}
std::string RenderGraph::debugDump() const { return impl_->dump; }

} // namespace aeg::render
