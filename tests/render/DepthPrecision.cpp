#include "RendererHarness.hpp"
#include <OrbitCamera.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
struct Image {
    unsigned width{}, height{};
    std::vector<unsigned char> rgb;
};
Image readPpm(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Image result;
    std::string magic;
    unsigned maximum{};
    input >> magic >> result.width >> result.height >> maximum;
    require(magic == "P6" && maximum == 255 && result.width && result.height && input.get() == '\n',
            "invalid depth precision capture");
    result.rgb.resize(std::size_t(result.width) * result.height * 3);
    input.read(reinterpret_cast<char*>(result.rgb.data()), std::streamsize(result.rgb.size()));
    require(std::size_t(input.gcount()) == result.rgb.size(), "truncated depth precision capture");
    return result;
}
aeg::assets::MeshData quad(float halfSize, float z, unsigned material) {
    aeg::assets::MeshData mesh;
    mesh.materialIndex = material;
    mesh.vertices = {{{-halfSize, -halfSize, z}, {0, 0, 1}, {0, 0}},
                     {{halfSize, -halfSize, z}, {0, 0, 1}, {1, 0}},
                     {{halfSize, halfSize, z}, {0, 0, 1}, {1, 1}},
                     {{-halfSize, halfSize, z}, {0, 0, 1}, {0, 1}}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}
aeg::assets::ModelData layers(bool reverseOrder) {
    aeg::assets::ModelData model;
    // Character-sized bounds and a 0.000625-unit decal gap, matching the
    // near-coincident but nonintersecting skin/decal geometry of the report.
    model.bounds.min = {-6.5f, -10, -2.6f};
    model.bounds.max = -model.bounds.min;
    model.materials.resize(3);
    model.materials[0].baseColor = {1, 0, 0, 1};
    model.materials[1].baseColor = {0, 1, 0, 1};
    auto& decal = model.materials[1];
    decal.alphaMasked = true;
    decal.texture = {4, 4, {}};
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            decal.texture.rgba.insert(decal.texture.rgba.end(),
                                      {255, 255, 255, static_cast<unsigned char>(x >= 2 && y >= 2 ? 0 : 255)});
    model.materials[2].baseColor = {0, 0, 1, 1};
    const auto body = quad(2, 0, 0), overlay = quad(1, 0.000625f, 1);
    model.meshes = reverseOrder ? std::vector{overlay, body} : std::vector{body, overlay};
    // A genuinely nearer surface must still occlude both layers.
    model.meshes.push_back(quad(0.15f, 0.2f, 2));
    return model;
}
void colorAt(const Image& image, const aeg::math::Mat4& vp, aeg::math::Vec3 point, unsigned channel, const std::string& label) {
    const auto clip = vp * aeg::math::Vec4(point, 1);
    const auto ndc = aeg::math::Vec3(clip) / clip.w;
    const int x = int((ndc.x * 0.5f + 0.5f) * float(image.width));
    const int y = int((ndc.y * 0.5f + 0.5f) * float(image.height));
    require(x > 0 && y > 0 && x + 1 < int(image.width) && y + 1 < int(image.height),
            label + " sample is outside viewport");
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const auto at = (std::size_t(y + dy) * image.width + std::size_t(x + dx)) * 3;
            for (unsigned c = 0; c < 3; ++c)
                require(std::abs(int(image.rgb[at + c]) - (c == channel ? 255 : 0)) <= 2,
                        label + " has an incorrectly occluded layer");
        }
}
}

int main(int argc, char** argv) {
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(800, 600, "AEG depth precision test", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    std::ostringstream diagnostics;
    auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
    int result{};
    try {
        const std::filesystem::path output(argv[1]);
        std::filesystem::create_directories(output);
        aeg::tests::TestRenderer renderer(window, true);
        aeg::render::RenderSettings settings;
        settings.unlit = true;
        settings.background = {0, 0, 0, 1};
        constexpr unsigned extent = 384;
        auto frame = [&] {
            bool drew{};
            for (int retry = 0; retry < 8 && !drew; ++retry) {
                glfwPollEvents();
                renderer.drawFrame(settings, [&] {
                    renderer.resizeViewport(extent, extent);
                    ImGui::Begin("Depth precision");
                    ImGui::Image(renderer.viewportTexture(), {384, 384});
                    ImGui::End();
                    drew = true;
                });
            }
            require(drew, "no depth precision frame submitted");
        };
        std::vector<aeg::render::AntialiasingSettings> modes{{}, {1, true, false}};
        if (renderer.supportedMsaaSamples() & 4u) modes.push_back({4, false, false});
        if (renderer.supportsTemporalAA()) modes.push_back({1, true, true});
        unsigned captureIndex{};
        for (const auto mode : modes) {
            renderer.setAntialiasing(mode);
            for (const bool reverseOrder : {false, true}) {
                const auto model = layers(reverseOrder);
                renderer.uploadModel(model);
                for (const float zoom : {6.0f, 8.0f})
                    for (const float yaw : {0.0f, 0.4f, 0.8f}) {
                        aeg::camera::OrbitCamera camera;
                        camera.fit(model.bounds);
                        camera.orbit(-yaw / 0.005f, 0);
                        camera.zoom(zoom);
                        settings.set_camera(camera.render_camera(1, {true, false}));
                        renderer.resetTemporalHistory();
                        for (unsigned i = 0; i < (mode.taa ? 16u : 2u); ++i) frame();
                        const auto label = "layers-" + std::to_string(captureIndex++);
                        const auto path = output / (label + ".ppm");
                        renderer.captureViewport(path);
                        const auto image = readPpm(path);
                        require(image.width == extent && image.height == extent, "stale depth precision extent");
                        for (const float y : {-0.65f, -0.4f, 0.4f, 0.65f})
                            for (const float x : {-0.65f, -0.4f, 0.4f, 0.65f}) {
                                const bool hole = x > 0 && y > 0;
                                colorAt(image, settings.viewProjection, {x, y, hole ? 0 : 0.000625f}, hole ? 0 : 1,
                                        label);
                            }
                        colorAt(image, settings.viewProjection, {0, 0, 0.2f}, 2, label + " foreground");
                    }
            }
        }
        require(renderer.validationErrorCount() == 0, "Vulkan validation error during depth precision test");
        renderer.waitIdle();
        std::cout << "PASS " << captureIndex << " close-view layered captures, alpha holes and foreground occlusion\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
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
