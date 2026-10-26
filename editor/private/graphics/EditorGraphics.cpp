#include <EditorBackend.hpp>
#include "../platform/NativeWindow.hpp"
#include <imgui_impl_glfw.h>
#include <stdexcept>

namespace aeg::editor {
struct EditorGraphics::Impl {
    EditorBackend backend;
    std::unique_ptr<render::Renderer> scene;
    std::unique_ptr<rhi::Sampler> sampler;
    std::shared_ptr<rhi::Texture> registeredOutput;
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
            backend.gui->updateTexture(texture, *output, *sampler);
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
    if (!impl_->backend.device || !impl_->backend.swapchain || !impl_->backend.gui)
        throw std::runtime_error("Editor backend factory returned incomplete graphics services");
    impl_->scene = std::make_unique<render::Renderer>(*impl_->backend.device);
    impl_->sampler = impl_->backend.device->createSampler({rhi::Filter::Linear, rhi::AddressMode::ClampToEdge});
}
EditorGraphics::~EditorGraphics() = default;
render::Renderer& EditorGraphics::renderer() { return *impl_->scene; }
ImTextureID EditorGraphics::viewportTexture() {
    impl_->updateViewportTexture();
    return impl_->texture;
}
void EditorGraphics::drawFrame(render::RenderSettings& settings, const std::function<void()>& buildUi) {
    auto& backend = impl_->backend;
    if (!backend.swapchain->beginFrame()) return;
    backend.gui->newFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    buildUi();
    ImGui::Render();
    auto& commands = backend.swapchain->commandList();
    impl_->scene->recordFrame(commands, settings);
    impl_->updateViewportTexture();
    const rhi::ClearValue clear{{0.02f, 0.025f, 0.035f, 1}};
    commands.beginRenderPass(backend.swapchain->renderPass(), backend.swapchain->framebuffer(),
                             backend.swapchain->extent(), std::span(&clear, 1));
    backend.gui->renderDrawData(commands, ImGui::GetDrawData());
    commands.endRenderPass();
    backend.swapchain->submit();
    impl_->scene->frameSubmitted();
    backend.swapchain->present();
}
}
