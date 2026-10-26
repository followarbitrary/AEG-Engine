#include "RendererHarness.hpp"
#include "PbrReference.hpp"
#include <RenderCamera.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace aeg;
using Color = std::array<double, 3>;
constexpr unsigned extent = 129;
constexpr math::Vec3 center{0, 0, -3}, normal{0, 0, 1}, base{0.42f, 0.28f, 0.16f};
constexpr float metallic = 0.15f, roughness = 0.75f;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<unsigned char> read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string magic;
    unsigned width{}, height{}, maximum{};
    input >> magic >> width >> height >> maximum;
    require(magic == "P6" && width == extent && height == extent && maximum == 255 && input.get() == '\n',
            "Invalid punctual-light capture");
    std::vector<unsigned char> result(extent * extent * 3);
    input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    require(input.gcount() == static_cast<std::streamsize>(result.size()), "Truncated punctual-light capture");
    return result;
}
int encoded(double linear) {
    require(std::isfinite(linear), "Punctual reference is nonfinite");
    // The renderer writes linear light into RGBA8 UNORM before capture applies
    // the sRGB transfer function. Model this quantization, especially near black.
    const auto value = std::round(std::clamp(linear, 0.0, 1.0) * 255) / 255;
    return int(std::lround(255 * (value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1 / 2.4) - 0.055)));
}
void pixel(const std::vector<unsigned char>& image, Color expected, const std::string& label, int x = extent / 2,
           int y = extent / 2, int tolerance = 3) {
    for (unsigned c = 0; c < 3; ++c) {
        const int actual = image[(std::size_t(y) * extent + x) * 3 + c], want = encoded(expected[c]);
        require(std::abs(actual - want) <= tolerance, label + " channel " + std::to_string(c) + " expected " +
                                                          std::to_string(want) + ", got " + std::to_string(actual));
    }
}
assets::ModelData plane() {
    assets::ModelData model;
    model.name = "Independent analytic punctual-light plane";
    auto& material = model.materials.emplace_back();
    material.baseColor = math::Vec4(base, 1);
    material.metallicFactor = metallic;
    material.roughnessFactor = roughness;
    auto& mesh = model.meshes.emplace_back();
    mesh.vertices = {{{-3, -3, -3}, normal, {0, 0}},
                     {{3, -3, -3}, normal, {1, 0}},
                     {{3, 3, -3}, normal, {1, 1}},
                     {{-3, 3, -3}, normal, {0, 1}}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return model;
}
// Independent double-precision distance/cone oracle; it neither includes nor
// duplicates the renderer's packing or shader helpers.
Color expected(const render::RenderSettings& settings, math::Vec3 point = center) {
    Color result{};
    for (unsigned c = 0; c < 3; ++c) result[c] = settings.ambient * base[c] * (1 - metallic);
    for (unsigned i = 0; i < std::min(settings.lightCount, render::kMaxLights); ++i) {
        const auto& light = settings.lights[i];
        if (!light.enabled) continue;
        math::Vec3 toward = -light.direction;
        double attenuation = 1;
        if (light.type != render::LightType::Directional) {
            toward = light.position - point;
            const double distance =
                std::sqrt(double(toward.x) * toward.x + double(toward.y) * toward.y + double(toward.z) * toward.z);
            if (distance == 0) continue;
            const double ratio = distance / light.range;
            const auto cutoff = std::max(1 - std::pow(ratio, 4), 0.0);
            attenuation = cutoff * cutoff / std::max(distance * distance, 0.0001);
            if (light.type == render::LightType::Spot) {
                const double directionLength = std::sqrt(double(light.direction.x) * light.direction.x +
                                                         double(light.direction.y) * light.direction.y +
                                                         double(light.direction.z) * light.direction.z);
                const double cosine = -(double(toward.x) * light.direction.x + double(toward.y) * light.direction.y +
                                        double(toward.z) * light.direction.z) /
                                      (distance * directionLength);
                const double inner = std::cos(double(light.innerConeAngle)),
                             outer = std::cos(double(light.outerConeAngle));
                const double t = inner == outer ? (cosine >= outer ? 1.0 : 0.0)
                                                : std::clamp((cosine - outer) / (inner - outer), 0.0, 1.0);
                attenuation *= inner == outer ? t : t * t * (3 - 2 * t);
            }
        }
        if (attenuation == 0) continue;
        const auto source = light.color * float(light.intensity * attenuation);
        const math::Vec3 radiance{std::min(source.x, 65504.0f), std::min(source.y, 65504.0f),
                                  std::min(source.z, 65504.0f)};
        const bool perspective = settings.viewProjection.at(3, 0) != 0 || settings.viewProjection.at(3, 1) != 0 ||
                                 settings.viewProjection.at(3, 2) != 0;
        const auto viewer = perspective ? settings.cameraPosition - point : settings.cameraTowardViewer;
        const auto value = tests::pbrReference(base, metallic, roughness, normal, toward, viewer, radiance);
        for (unsigned c = 0; c < 3; ++c) result[c] += value[c];
    }
    return result;
}
}

int main(int argc, char** argv) {
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(320, 240, "AEG punctual-light test", nullptr, nullptr);
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
        tests::TestRenderer renderer(window, true);
        render::RenderSettings settings;
        settings.ambient = 0;
        settings.background = {0, 0, 0, 1};
        settings.normalMapping = false;
        const auto camera = [&](bool perspective = false, math::ClipSpaceConvention clip = {true, false}) {
            settings.set_camera({math::identity(),
                                 perspective ? math::perspective(1, 1, 0.1f, 20, clip)
                                             : math::orthographic(-2, 2, -2, 2, 0.1f, 20, clip),
                                 clip});
        };
        camera();
        renderer.uploadModel(plane());
        const auto frame = [&] {
            bool drew{};
            for (unsigned retry = 0; retry < 8 && !drew; ++retry) {
                glfwPollEvents();
                renderer.drawFrame(settings, [&] {
                    renderer.resizeViewport(extent, extent);
                    ImGui::Begin("Punctual analytical viewport");
                    ImGui::Image(renderer.viewportTexture(), {128, 128});
                    ImGui::End();
                    drew = true;
                });
            }
            require(drew, "Punctual frame was not submitted");
        };
        unsigned captures{};
        const auto check = [&](const std::string& label) {
            frame();
            frame();
            const auto path = output / ("punctual-" + std::to_string(captures++) + ".ppm");
            renderer.captureViewport(path);
            pixel(read(path), expected(settings), label);
        };
        auto& light = settings.lights[0];
        light.type = render::LightType::Point;
        // Distance squared overflows float here, although source radiance times
        // inverse square is representable. The oracle evaluates it in double.
        light.position = {0, 0, 2e19f};
        light.color = {1, 1, 1};
        light.intensity = 3e38f;
        light.range = 2e20f;
        check("Far finite point light retains representable incident radiance");
        light.color = {0.65f, 0.85f, 1};
        light.intensity = 2;
        light.range = 20;
        // These are absolute BRDF predictions, so omitting inverse-square
        // attenuation cannot pass as a merely monotonic brightness change.
        for (const float distance : {1.0f, 2.0f, 3.0f}) {
            light.position = center + normal * distance;
            check("Point distance " + std::to_string(distance));
        }
        light.position = {0, 0, 997};
        light.range = 100000;
        light.intensity = 1000000;
        check("High source intensity is attenuated before finite incident-radiance cap");
        light.position = center + normal * 0.005f;
        light.range = 1;
        light.intensity = 0.00002f;
        check("Near-source inverse square uses the 0.01 world-unit distance floor");
        light.intensity = 2;
        light.range = 2;
        for (const float distance : {1.0f, 1.5f, 1.9f, 2.0f, 2.1f, 0.0f}) {
            light.position = center + normal * distance;
            check("Point finite-range/coincidence " + std::to_string(distance));
        }
        light.type = render::LightType::Spot;
        light.position = {0, 0, -1};
        light.range = 10;
        light.innerConeAngle = math::to_radians(15);
        light.outerConeAngle = math::to_radians(35);
        light.intensity = 4;
        for (const float degrees : {0.0f, 15.0f, 25.0f, 35.0f, 45.0f}) {
            const auto angle = math::to_radians(degrees);
            light.direction = {std::sin(angle), 0, -std::cos(angle)};
            check("Spot inner/transition/outer " + std::to_string(degrees));
        }
        light.innerConeAngle = light.outerConeAngle = math::to_radians(25);
        for (const float degrees : {24.0f, 26.0f}) {
            const auto angle = math::to_radians(degrees);
            light.direction = {std::sin(angle), 0, -std::cos(angle)};
            check("Equal cone hard edge " + std::to_string(degrees));
        }
        light.innerConeAngle = light.outerConeAngle = 0;
        light.direction = {0, 0, -1};
        check("Equal zero-angle cone includes its exact axial hard edge");
        settings.ambient = 0.17f;
        settings.lightCount = 0;
        check("No direct lights keeps one ambient contribution");
        settings.lightCount = 3;
        light.type = render::LightType::Directional;
        light.direction = {0, 0, -1};
        light.color = {1, 0, 0};
        light.intensity = 0.65f;
        settings.lights[1] = light;
        settings.lights[1].type = render::LightType::Point;
        settings.lights[1].position = {0, 0, -1};
        settings.lights[1].range = 9;
        settings.lights[1].color = {0, 1, 0};
        settings.lights[1].intensity = 2;
        settings.lights[2] = settings.lights[1];
        settings.lights[2].type = render::LightType::Spot;
        settings.lights[2].color = {0, 0, 1};
        settings.lights[2].innerConeAngle = math::to_radians(15);
        settings.lights[2].outerConeAngle = math::to_radians(35);
        check("Colored directional/point/spot sum with ambient once");
        settings.lights[1].enabled = false;
        check("Disabled light contributes no direct radiance");
        for (const bool perspective : {false, true})
            for (const bool down : {false, true})
                for (const bool reversed : {false, true}) {
                    camera(perspective, {down, reversed});
                    check("Punctual camera and clip conventions");
                }
        if (renderer.supportsTemporalAA()) {
            camera();
            renderer.setAntialiasing({1, false, true});
            settings.lightCount = 1;
            const std::vector<std::function<void(render::Light&)>> changes{
                [](auto& l) { l.type = render::LightType::Spot; },
                [](auto& l) { l.position.x += 0.1f; },
                [](auto& l) { l.direction.x += 0.1f; },
                [](auto& l) { l.color.x += 0.1f; },
                [](auto& l) { l.intensity += 0.1f; },
                [](auto& l) { l.range += 1; },
                [](auto& l) { l.innerConeAngle += 0.01f; },
                [](auto& l) { l.outerConeAngle += 0.01f; },
                [](auto& l) { l.enabled = !l.enabled; }};
            for (const auto& change : changes) {
                frame();
                frame();
                require(renderer.temporalFrameCount() >= 2, "Punctual TAA did not accumulate");
                change(light);
                frame();
                require(renderer.temporalFrameCount() == 1, "Active light property retained stale TAA history");
            }
            frame();
            settings.lightCount = 2;
            frame();
            require(renderer.temporalFrameCount() == 1, "Light count retained stale TAA history");
            frame();
            const auto count = renderer.temporalFrameCount();
            settings.model = math::translation({0.02f, 0, 0});
            frame();
            require(renderer.temporalFrameCount() == count + 1, "Model motion reset punctual TAA");
            settings.set_camera({math::translation({-0.02f, 0, 0}),
                                 math::orthographic(-2, 2, -2, 2, 0.1f, 20, {true, false}),
                                 {true, false}});
            frame();
            require(renderer.temporalFrameCount() == count + 2, "Camera motion reset punctual TAA");
        }
        renderer.waitIdle();
        require(renderer.validationErrorCount() == 0 &&
                    diagnostics.str().find("Vulkan validation error") == std::string::npos,
                "Punctual path produced Vulkan validation errors");
        std::cout << "Punctual lights: " << captures
                  << " analytic captures; distance, range, cones, sums and TAA; Vulkan errors: 0\n";
    } catch (const std::exception& error) {
        std::cout << "Punctual-light test failed: " << error.what() << '\n';
        result = 1;
    }
    std::cerr.rdbuf(previous);
    if (result) std::cerr << diagnostics.str();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
