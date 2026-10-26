#include "RendererHarness.hpp"
#include <RenderCamera.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using namespace aeg;
constexpr unsigned extent = 128;
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
struct Image {
    std::vector<unsigned char> rgb;
};
Image read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::string magic;
    unsigned width{}, height{}, maximum{};
    file >> magic >> width >> height >> maximum;
    require(magic == "P6" && width == extent && height == extent && maximum == 255 && file.get() == '\n',
            "Invalid convention capture");
    Image result{std::vector<unsigned char>(extent * extent * 3)};
    file.read(reinterpret_cast<char*>(result.rgb.data()), result.rgb.size());
    require(file.gcount() == std::streamsize(result.rgb.size()), "Truncated convention capture");
    return result;
}
assets::MeshData quad(float left, float right, float bottom, float top, float z, unsigned material) {
    assets::MeshData mesh;
    mesh.materialIndex = material;
    mesh.vertices = {{{left, bottom, z}, {0, 0, 1}, {}}, {{right, bottom, z}, {0, 0, 1}, {}},
                     {{right, top, z}, {0, 0, 1}, {}}, {{left, top, z}, {0, 0, 1}, {}}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}
assets::ModelData layers(bool reverseOrder) {
    assets::ModelData model;
    model.materials.resize(3);
    model.materials[0].baseColor = {1, 0, 0, 1};
    model.materials[1].baseColor = {0, 0, 1, 1};
    model.materials[2].baseColor = {0, 1, 0, 1};
    model.meshes = {quad(-1, 1, 0, 1, -3, 0), quad(-1, 1, -1, 0, -3, 1),
                    quad(-0.25f, 0.25f, -0.5f, 0.5f, -2, 2)};
    if (reverseOrder) std::reverse(model.meshes.begin(), model.meshes.end());
    return model;
}
void sample(const Image& image, const math::Mat4& canonical, math::Vec3 point, unsigned channel,
            const std::string& label) {
    const auto clip = canonical * math::Vec4(point, 1);
    const int x = int((clip.x / clip.w * 0.5f + 0.5f) * extent);
    const int y = int((clip.y / clip.w * 0.5f + 0.5f) * extent);
    require(x > 2 && y > 2 && x + 2 < int(extent) && y + 2 < int(extent), "Invalid convention probe");
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            for (unsigned c = 0; c < 3; ++c)
                require(std::abs(int(image.rgb[((y + dy) * extent + x + dx) * 3 + c]) -
                                 (c == channel ? 255 : 0)) <= 2,
                        label + " inverted image orientation or selected the farther surface");
}
void nearImage(const Image& image, const Image& reference, const std::string& label) {
    require(image.rgb.size() == reference.rgb.size(), "Convention capture extent changed");
    for (std::size_t i = 0; i < image.rgb.size(); ++i)
        require(std::abs(int(image.rgb[i]) - int(reference.rgb[i])) <= 2,
                label + " changed pixels when only clip convention changed");
}
assets::ModelData ramp() {
    assets::ModelData model;
    auto mesh = quad(-1, 1, -0.9f, 0.9f, -3, 0);
    mesh.vertices[0].uv = {0, 0};
    mesh.vertices[1].uv = {1, 0};
    mesh.vertices[2].uv = {1, 1};
    mesh.vertices[3].uv = {0, 1};
    model.meshes.push_back(std::move(mesh));
    auto& material = model.materials.emplace_back();
    material.hasTexture = true;
    material.texture.width = 256;
    material.texture.rgba.resize(256 * 4);
    for (unsigned x = 0; x < 256; ++x) {
        const auto value = static_cast<unsigned char>(48 + 160 * std::min(x % 128, 128 - x % 128) / 64);
        for (unsigned c = 0; c < 4; ++c) material.texture.rgba[x * 4 + c] = c == 3 ? 255 : value;
    }
    return model;
}
double rampError(const Image& moved, const Image& stationary, int shift) {
    double error{};
    unsigned count{};
    for (unsigned y = extent / 3; y < extent * 2 / 3; ++y)
        for (unsigned x = extent / 4; x < extent * 3 / 4; ++x) {
            const auto from = (y * extent + x - shift) * 3;
            const int left = stationary.rgb[from - 6], center = stationary.rgb[from], right = stationary.rgb[from + 6];
            // Textured slopes expose a history lag even when neighborhood clamping hides flat silhouette trails.
            if (std::abs(left + right - 2 * center) > 2 || std::abs(right - left) < 8) continue;
            error += std::abs(int(moved.rgb[(y * extent + x) * 3]) - center);
            ++count;
        }
    require(count > 300, "Convention ramp has insufficient measurable slope pixels");
    return error / count;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(320, 240, "AEG clip convention test", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    std::ostringstream diagnostics;
    auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
    int result{};
    try {
        const std::filesystem::path output(argv[1]);
        std::filesystem::create_directories(output);
        tests::TestRenderer renderer(window, true);
        render::RenderSettings settings;
        settings.unlit = true;
        settings.background = {0, 0, 0, 1};
        auto frame = [&] {
            bool drew{};
            for (unsigned retry = 0; retry < 8 && !drew; ++retry) {
                glfwPollEvents();
                renderer.drawFrame(settings, [&] {
                    renderer.resizeViewport(extent, extent);
                    ImGui::Begin("Conventions");
                    ImGui::Image(renderer.viewportTexture(), {128, 128});
                    ImGui::End();
                    drew = true;
                });
            }
            require(drew, "No convention frame submitted");
        };
        unsigned captureIndex{};
        auto capture = [&] {
            const auto path = output / ("convention-" + std::to_string(captureIndex++) + ".ppm");
            renderer.captureViewport(path);
            return read(path);
        };
        std::vector<render::AntialiasingSettings> modes{{}, {1, true, false}};
        if (renderer.supportsTemporalAA()) {
            modes.push_back({1, false, true});
            modes.push_back({1, true, true});
        }
        for (const auto mode : modes) {
            renderer.setAntialiasing(mode);
            for (const bool perspective : {false, true}) {
                Image reference;
                bool first = true;
                for (const bool yDown : {false, true})
                    for (const bool reversed : {false, true}) {
                        const math::ClipSpaceConvention convention{yDown, reversed};
                        const auto projection = perspective
                            ? math::perspective(math::to_radians(60), 1, 0.1f, 10, convention)
                            : math::orthographic(-1.5f, 1.5f, -1.5f, 1.5f, 0.1f, 10, convention);
                        settings.set_camera({math::identity(), projection, convention});
                        // Switch convention on an already populated temporal history, without an explicit reset.
                        if (first) renderer.uploadModel(layers(false));
                        frame();
                        if (mode.taa)
                            require(renderer.temporalFrameCount() == 1, "Convention or projection change retained history");
                        for (unsigned i = 1; i < (mode.taa ? 12u : 2u); ++i) frame();
                        const auto image = capture();
                        const auto canonical = perspective
                            ? math::perspective(math::to_radians(60), 1, 0.1f, 10, {true, reversed})
                            : math::orthographic(-1.5f, 1.5f, -1.5f, 1.5f, 0.1f, 10, {true, reversed});
                        const auto label = std::string(perspective ? "Perspective" : "Orthographic") +
                            (yDown ? " Y-down" : " Y-up") + (reversed ? " reverse" : " normal");
                        sample(image, canonical, {0, 0.875f, -3}, 0, label);
                        sample(image, canonical, {0, -0.875f, -3}, 2, label);
                        sample(image, canonical, {0, 0, -2}, 1, label);
                        // The world-space upper red probe must be physically above the lower blue probe.
                        require(image.rgb[(extent / 4 * extent + extent / 2) * 3] > 250 &&
                                    image.rgb[(extent * 3 / 4 * extent + extent / 2) * 3 + 2] > 250,
                                label + " did not preserve world-up image orientation");
                        if (first) reference = image;
                        else nearImage(image, reference, label);
                        first = false;
                        // Draw far surfaces after the foreground too; depth must win in both draw orders.
                        renderer.uploadModel(layers(true));
                        for (unsigned i = 0; i < (mode.taa ? 12u : 2u); ++i) frame();
                        nearImage(capture(), image, label + " reversed draw order");
                        if (mode.taa) {
                            // Actual camera/model reprojection under each depth convention.
                            settings.model = math::translation({0.125f, 0, 0});
                            frame();
                            const auto moved = capture();
                            sample(moved, canonical, {0.125f, 0, -2}, 1, label + " temporal motion");
                            settings.model = math::identity();
                            frame();
                            sample(capture(), canonical, {0, 0, -2}, 1, label + " temporal return");
                        }
                    }
            }
        }
        if (renderer.supportsTemporalAA()) {
            renderer.setAntialiasing({1, false, true});
            renderer.uploadModel(ramp());
            for (const bool perspective : {false, true})
                for (const bool yDown : {false, true})
                    for (const bool reversed : {false, true}) {
                        const math::ClipSpaceConvention convention{yDown, reversed};
                        const auto projection = perspective
                            ? math::perspective(math::to_radians(60), 1, 0.1f, 10, convention)
                            : math::orthographic(-1.5f, 1.5f, -1.5f, 1.5f, 0.1f, 10, convention);
                        settings.model = math::identity();
                        settings.set_camera({math::identity(), projection, convention});
                        renderer.resetTemporalHistory();
                        for (unsigned i = 0; i < 32; ++i) frame();
                        const auto stationary = capture();
                        // Four physical pixels of camera motion, then two opposite pixels of model motion.
                        settings.viewProjection = math::translation({8.0f / extent, 0, 0}) * projection;
                        frame();
                        const auto cameraError = rampError(capture(), stationary, 4);
                        const float clipW = perspective ? 3.0f : 1.0f;
                        settings.model = math::translation({-4.0f * clipW / (extent * projection[0][0]), 0, 0});
                        frame();
                        const auto modelError = rampError(capture(), stationary, 2);
                        require(cameraError < 0.75 && modelError < 0.75,
                                "Convention camera/model reprojection left a textured history lag: " +
                                    std::to_string(cameraError) + ", " + std::to_string(modelError));
                    }
        }
        require(renderer.validationErrorCount() == 0, "Vulkan validation failed for clip conventions");
        renderer.waitIdle();
        std::cout << "PASS " << captureIndex << " clip captures: four flags, perspective/orthographic, depth, "
                     "orientation, FXAA/TAA, history switches and motion\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::cerr.rdbuf(previous);
    if (diagnostics.str().find("[Vulkan error]") != std::string::npos) result = 1;
    if (result) std::cerr << diagnostics.str();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
