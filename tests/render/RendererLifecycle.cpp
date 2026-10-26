#include <ModelImporter.hpp>
#include "RendererHarness.hpp"
#include <OrbitCamera.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
std::vector<char> readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("missing lifecycle capture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}
int main(int argc, char** argv) {
    if (argc != 4 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(800, 600, "AEG lifecycle test", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    std::ostringstream diagnostics;
    auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
    int result = 0;
    try {
        const std::filesystem::path runtime(argv[1]), fixture(argv[2]), output(argv[3]);
        aeg::tests::TestRenderer renderer(window, true);
        auto source = aeg::assets::loadModel(runtime / "assets/models/demo.obj");
        renderer.uploadModel(source);
        aeg::camera::OrbitCamera camera;
        camera.fit(source.bounds);
        aeg::render::RenderSettings settings;
        ImTextureID viewportId{};
        auto render = [&](std::uint32_t width, std::uint32_t height) {
            bool drew = false;
            for (int retry = 0; retry < 5 && !drew; ++retry) {
                glfwPollEvents();
                renderer.drawFrame(settings, [&] {
                    renderer.resizeViewport(width, height);
                    settings.set_camera(camera.render_camera(float(width) / float(height), {true, false}));
                    ImGui::SetNextWindowPos({40, 60}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize({500, 400}, ImGuiCond_Always);
                    ImGui::Begin("Scaled viewport");
                    const auto currentId = renderer.viewportTexture();
                    if (viewportId && viewportId != currentId)
                        throw std::runtime_error("viewport replacement changed the stable GUI texture ID");
                    viewportId = currentId;
                    ImGui::Image(currentId, {400, 300});
                    ImGui::End();
                    drew = true;
                });
            }
            if (!drew) throw std::runtime_error("no frame rendered after resize");
        };
        for (const auto size : std::array<std::array<int, 2>, 4>{{{320, 240}, {960, 540}, {240, 640}, {800, 600}}}) {
            glfwSetWindowSize(window, size[0], size[1]);
            render(size[0], size[1]);
        }
        renderer.captureViewport(output);
        const auto before = readFile(output);
        auto invalid = source;
        invalid.meshes.at(0).indices.at(0) = 0xffffffffu;
        bool rejected = false;
        try {
            renderer.uploadModel(invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        if (!rejected) throw std::runtime_error("invalid replacement was accepted");
        render(800, 600);
        renderer.captureViewport(output);
        if (before != readFile(output)) throw std::runtime_error("failed replacement changed the displayed model");
        auto replacement = aeg::assets::loadModel(fixture);
        renderer.uploadModel(replacement);
        camera.fit(replacement.bounds);
        render(800, 600);
        renderer.captureViewport(output);
        if (before == readFile(output)) throw std::runtime_error("successful replacement did not change the scene");
        renderer.waitIdle();
        if (renderer.validationErrorCount() != 0) throw std::runtime_error("Vulkan lifecycle validation failed");
    } catch (const std::exception& error) {
        std::cout << "FAIL lifecycle: " << error.what() << '\n';
        result = 1;
    }
    std::cerr.rdbuf(previous);
    if (diagnostics.str().find("[Vulkan error]") != std::string::npos) {
        std::cerr << diagnostics.str();
        result = 1;
    }
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    if (!result)
        std::cout << "PASS window/viewport resize, rejected replacement preserves pixels, successful replacement, "
                     "clean teardown\n";
    return result;
}
