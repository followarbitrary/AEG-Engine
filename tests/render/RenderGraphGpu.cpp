#include "RendererHarness.hpp"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
std::size_t nonzeroBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("render graph smoke capture is missing");
    std::string magic;
    int width = 0, height = 0, maximum = 0;
    input >> magic >> width >> height >> maximum;
    if (!input || magic != "P6" || width < 64 || height < 64 || maximum != 255)
        throw std::runtime_error("render graph smoke capture has an invalid PPM header");
    input.get();
    std::size_t count = 0;
    char byte = 0;
    while (input.get(byte))
        if (static_cast<unsigned char>(byte) != 0) ++count;
    return count;
}
}

int main(int argc, char** argv) {
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(320, 240, "AEG render graph smoke", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    int result = 0;
    try {
        aeg::tests::TestRenderer renderer(window, true);
        aeg::render::RenderSettings settings;
        const auto capture = std::filesystem::path(argv[1]);
        for (int frame = 0; frame < 3; ++frame) {
            renderer.drawFrame(settings, [&] {
                renderer.resizeViewport(320, 240);
                ImGui::Begin("Render graph smoke");
                ImGui::Text("frame %d", frame);
                ImGui::End();
            });
        }
        renderer.captureViewport(capture);
        renderer.waitIdle();
        if (!nonzeroBytes(capture)) throw std::runtime_error("render graph smoke produced an empty frame");
        if (renderer.validationErrorCount() != 0) throw std::runtime_error("Vulkan validation reported an error");
        const auto& dump = renderer.renderGraphDump();
        if (dump.find("Clear swapchain") == std::string::npos ||
            dump.find("ImGui") == std::string::npos || dump.find("culled=false") == std::string::npos)
            throw std::runtime_error("render graph dump omitted retained clear and ImGui passes");
        std::cout << "PASS render graph GPU smoke\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL render graph GPU smoke: " << error.what() << '\n';
        result = 1;
    }
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
