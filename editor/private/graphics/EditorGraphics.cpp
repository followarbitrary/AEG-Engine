#include <EditorBackend.hpp>
#include <RenderGraph.hpp>
#include "TransientTexturePool.hpp"
#include "../platform/NativeWindow.hpp"
#include <imgui_impl_glfw.h>
#include <stdexcept>

namespace aeg::editor {
struct EditorGraphics::Impl {
    EditorBackend backend;
    std::unique_ptr<render::Renderer> scene;
    std::unique_ptr<rhi::Sampler> sampler;
    std::shared_ptr<rhi::Texture> registeredOutput;
    std::unique_ptr<render::TransientTexturePool> graphPool;
    render::FrameToken graphFrame{};
    std::string graphDump;
    bool graphDebug{};
    ImTextureID texture{};
    bool platformInitialized{};

    ~Impl() {
        if (backend.device) {
            try {
                backend.device->waitIdle();
            } catch (...) {
            }
        }
        if (texture && backend.gui) backend.gui->unregisterTexture(texture);
        registeredOutput.reset();
        backend.gui.reset();
        sampler.reset();
        scene.reset();
        graphPool.reset();
        backend.swapchain.reset();
        backend.device.reset();
        if (platformInitialized) ImGui_ImplGlfw_Shutdown();
    }
    void updateViewportTexture() {
        auto output = scene->outputTexture();
        if (!output) throw std::runtime_error("Viewport output is unavailable");
        if (!texture)
            texture = backend.gui->registerTexture(*output, *sampler);
        else if (output != registeredOutput)
            texture = backend.gui->updateTextureForFrame(texture, *output, *sampler);
        // Release the previous view only after the descriptor stops referring to it.
        registeredOutput = std::move(output);
    }
};

EditorGraphics::EditorGraphics(GLFWwindow* window, const EditorBackendFactory& factory, bool validation)
    : impl_(std::make_unique<Impl>()) {
    if (!ImGui::GetCurrentContext()) throw std::runtime_error("Editor graphics requires an ImGui context");
    const auto native = nativeWindow(window);
    if (!ImGui_ImplGlfw_InitForOther(window, true)) throw std::runtime_error("ImGui GLFW initialization failed");
    impl_->platformInitialized = true;
    impl_->backend = factory(native, validation);
    impl_->graphDebug = validation;
    if (!impl_->backend.device || !impl_->backend.swapchain || !impl_->backend.gui)
        throw std::runtime_error("Editor backend factory returned incomplete graphics services");
    impl_->scene = std::make_unique<render::Renderer>(*impl_->backend.device,
                                                      impl_->backend.swapchain->framesInFlight(), true);
    impl_->graphPool = std::make_unique<render::TransientTexturePool>(*impl_->backend.device);
    impl_->sampler = impl_->backend.device->createSampler({rhi::Filter::Linear, rhi::AddressMode::ClampToEdge});
}
EditorGraphics::~EditorGraphics() = default;
render::Renderer& EditorGraphics::renderer() { return *impl_->scene; }
ImTextureID EditorGraphics::viewportTexture() {
    impl_->updateViewportTexture();
    return impl_->texture;
}
const std::string& EditorGraphics::renderGraphDump() const { return impl_->graphDump; }
std::span<const rhi::GpuPassTiming> EditorGraphics::gpuPassTimings() const {
    if (!impl_->backend.swapchain) return {};
    return impl_->backend.swapchain->gpuPassTimings();
}
void EditorGraphics::drawFrame(render::RenderSettings& settings, const std::function<void()>& buildUi) {
    auto& backend = impl_->backend;
    if (!backend.swapchain->beginFrame()) return;
    backend.gui->beginFrame(backend.swapchain->currentFrameSlot(), backend.swapchain->framesInFlight());
    const auto frameToken = ++impl_->graphFrame;
    impl_->graphPool->beginFrame(frameToken, backend.swapchain->currentFrameSlot(),
                                 backend.swapchain->framesInFlight());
    backend.gui->newFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    buildUi();
    ImGui::Render();
    auto& commands = backend.swapchain->commandList();
    impl_->scene->recordFrame(commands, settings);
    impl_->updateViewportTexture();
    render::RenderGraph graph;
    const auto swapchain = graph.importSwapchain(*backend.swapchain, "swapchain");
    auto clearPass = graph.addPass("Clear swapchain", render::RenderGraph::PassType::Graphics,
                                   render::RenderGraph::PassFlags::None);
    clearPass.clear({{0.02f, 0.025f, 0.035f, 1.0f}, 1.0f, 0})
        .write(swapchain, render::RenderGraph::Usage::ColorWrite);
    graph.addPass("ImGui", render::RenderGraph::PassType::Graphics,
                  render::RenderGraph::PassFlags::NeverCull,
                  [&](render::RenderGraph::PassContext&) { backend.gui->renderDrawData(commands, ImGui::GetDrawData()); })
        .write(swapchain, render::RenderGraph::Usage::ColorWrite);
    const auto compiled = graph.compile({swapchain});
    (void)compiled;
    if (impl_->graphDebug)
        impl_->graphDump = graph.debugDump();
    else
        impl_->graphDump.clear();
    graph.execute(commands, *backend.device, *impl_->graphPool, frameToken);
    backend.swapchain->submit();
    impl_->scene->frameSubmitted();
    backend.swapchain->present();
}
}
