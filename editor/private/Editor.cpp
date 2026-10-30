#include <Editor.hpp>
#include "EditorState.hpp"
#include "fonts/EditorFont.hpp"
#include <ModelImporter.hpp>
#include <Utf8Path.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

namespace aeg::editor {
int runEditor(const EditorOptions& options, const EditorBackendFactory& factory) {
    WindowLifetime lifetime;
    initializeEditorWindow(lifetime, options);
    configureStyle();
    const auto font = configureEditorFont(options.resourceDirectory, options.fontPath);
    std::cout << "Font: " << foundation::pathToUtf8(font.source) << '\n';

    // This scope makes backend teardown precede the ImGui context and native window.
    EditorGraphics graphics(lifetime.window, factory, options.validation);
    auto& renderer = graphics.renderer();
    renderer.setAntialiasing({options.msaaSamples, options.fxaa, options.taa});
    renderer.setToneMapping(options.toneMapping);
    EditorState state;
    state.antialiasing = renderer.antialiasing();
    state.supportedMsaa = renderer.supportedMsaaSamples();
    state.supportedTaa = renderer.supportsTemporalAA();
    if (state.antialiasing.msaaSamples != options.msaaSamples) {
        if (state.antialiasing.taa)
            state.antialiasingNotice = "TAA 替代 MSAA，有效采样数为 1。";
        else
            state.antialiasingNotice = "请求 MSAA " + std::to_string(options.msaaSamples) + "x，当前设备已降级为 " +
                                       std::to_string(state.antialiasing.msaaSamples) + "x。";
        std::cerr << state.antialiasingNotice << '\n';
    }
    std::cout << "Antialiasing: MSAA " << state.antialiasing.msaaSamples << "x | FXAA "
              << (state.antialiasing.fxaa ? "on" : "off") << " | TAA " << (state.antialiasing.taa ? "on" : "off")
              << '\n';
    std::cout << "GPU: " << renderer.deviceName() << '\n';
    state.model() =
        assets::loadModel(options.initialModel.empty() ? options.resourceDirectory / "assets" / "models" / "demo.obj"
                                                       : options.initialModel);
    renderer.uploadModel(state.model());
    std::cout << "Model: " << foundation::pathToUtf8(state.model().source) << " (" << state.model().vertexCount()
              << " vertices, " << state.model().triangleCount() << " triangles)\n";
    state.camera.fit(state.model().bounds);
    renderer.resetTemporalHistory();
    const auto initialPath = foundation::pathToUtf8(state.model().source);
    std::snprintf(state.importPath.data(), state.importPath.size(), "%s", initialPath.c_str());
    bindImportQueue(lifetime.window, state.imports);
    double previousTime = glfwGetTime();

    while (!glfwWindowShouldClose(lifetime.window) && (options.frameLimit == 0 || state.frames < options.frameLimit)) {
        glfwPollEvents();
        int framebufferWidth = 0, framebufferHeight = 0;
        glfwGetFramebufferSize(lifetime.window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth == 0 || framebufferHeight == 0) {
            glfwWaitEventsTimeout(0.05);
            previousTime = glfwGetTime();
            continue;
        }
        processImports(state, renderer);
        const double now = glfwGetTime();
        const auto elapsed = static_cast<float>(std::clamp(now - previousTime, 0.0, 0.1));
        previousTime = now;
        if (!state.gizmo.active) state.systems.run(state.scene, elapsed);
        bool builtUi = false;
        graphics.drawFrame(state.settings, [&] {
            builtUi = true;
            drawWorkspace(renderer);
            drawScenePanel(state);
            drawImportPanel(state);
            // The inspector applies AA before the viewport records its image descriptor.
            drawInspectorPanel(state, renderer);
            drawViewportPanel(state, renderer, graphics);
        });
        if (builtUi) ++state.frames;
    }
    renderer.waitIdle();
    if (!options.capturePath.empty()) {
        if (!state.sceneRendered) throw std::runtime_error("无法截图：视口尚未渲染场景。");
        renderer.captureViewport(options.capturePath);
        std::cout << "Capture: " << foundation::pathToUtf8(options.capturePath) << '\n';
    }
    const auto errors = renderer.validationErrorCount();
    std::cout << "Rendered frames: " << state.frames << "\nValidation errors: " << errors << '\n';
    glfwSetDropCallback(lifetime.window, nullptr);
    glfwSetWindowUserPointer(lifetime.window, nullptr);
    return errors == 0 ? 0 : 2;
}
}
