#include "RendererHarness.hpp"
#include <GLFW/glfw3.h>
#include <RenderCamera.hpp>
#include <imgui.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace aeg;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

assets::ModelData triangleModel() {
    assets::ModelData model;
    model.name = "three-frame triangle";
    model.materials.emplace_back().baseColor = {0.15f, 0.7f, 0.95f, 1};
    auto& mesh = model.meshes.emplace_back();
    mesh.name = "triangle";
    mesh.vertices = {{{-1.3f, -1.1f, -3.0f}, {0, 0, 1}, {0, 0}},
                     {{1.3f, -1.1f, -3.0f}, {0, 0, 1}, {1, 0}},
                     {{0.0f, 1.3f, -3.0f}, {0, 0, 1}, {0.5f, 1}}};
    mesh.indices = {0, 1, 2};
    return model;
}

std::size_t nonzeroBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "three-frame capture is missing");
    std::string magic;
    int width = 0, height = 0, maximum = 0;
    input >> magic >> width >> height >> maximum;
    require(input && magic == "P6" && width >= 64 && height >= 64 && maximum == 255,
            "three-frame capture has an invalid PPM header");
    input.get();
    std::size_t nonzero = 0;
    char byte = 0;
    while (input.get(byte))
        if (static_cast<unsigned char>(byte) != 0) ++nonzero;
    return nonzero;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(320, 240, "AEG three-frame test", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    int result = 0;
    try {
        const auto output = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(output.parent_path());
        {
            tests::TestRenderer renderer(window, true, 3);
            renderer.uploadModel(triangleModel());
            render::RenderSettings settings;
            settings.background = {0, 0, 0, 1};
            settings.ambient = 0.2f;
            settings.lights[0].direction = {0, 0, -1};
            settings.lights[0].intensity = 1.5f;
            settings.set_camera({math::look_at({0, 0, 0}, {0, 0, -3}, {0, 1, 0}),
                                 math::orthographic(-2, 2, -2, 2, 0.1f, 20, {true, false}), {true, false}});
            renderer.resizeViewport(320, 240);

            unsigned submitted = 0;
            for (unsigned frame = 0; frame < 12; ++frame) {
                bool drew = false;
                for (unsigned retry = 0; retry < 8 && !drew; ++retry) {
                    glfwPollEvents();
                    renderer.drawFrame(settings, [&] {
                        ImGui::Begin("Three-frame viewport");
                        ImGui::Text("frame %u", frame);
                        ImGui::Image(renderer.viewportTexture(), {256, 192});
                        ImGui::End();
                        drew = true;
                    });
                }
                require(drew, "three-frame test could not submit a frame");
                ++submitted;
            }
            require(submitted >= 12, "three-frame test rendered fewer than twelve frames");
            renderer.captureViewport(output);
            renderer.waitIdle();
            require(renderer.validationErrorCount() == 0, "Vulkan validation reported an error");
            require(nonzeroBytes(output) != 0, "three-frame capture is empty");
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL three-frame GPU regression: " << error.what() << '\n';
        result = 1;
    }
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    if (!result) std::cout << "PASS three-frame Vulkan swapchain regression (12 frames, validation clean)\n";
    return result;
}
