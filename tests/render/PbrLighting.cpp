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
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace aeg;
using Color = std::array<double, 3>;
constexpr unsigned extent = 129; // Center pixel lies exactly on the center ray.
constexpr math::Vec3 base{0.7f, 0.3f, 0.1f};
constexpr math::Vec3 normal{0, 0, 1};
constexpr math::Vec3 center{0, 0, -3};
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct Image {
    std::vector<unsigned char> rgb;
};
Image read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string magic;
    unsigned width{}, height{}, maximum{};
    input >> magic >> width >> height >> maximum;
    require(magic == "P6" && width == extent && height == extent && maximum == 255 && input.get() == '\n',
            "Invalid PBR capture header");
    Image result{std::vector<unsigned char>(extent * extent * 3)};
    input.read(reinterpret_cast<char*>(result.rgb.data()), std::streamsize(result.rgb.size()));
    require(input.gcount() == std::streamsize(result.rgb.size()) && input.peek() == std::char_traits<char>::eof(),
            "PBR capture is truncated or oversized");
    return result;
}
int encoded(double value) {
    require(std::isfinite(value), "Nonfinite PBR reference value");
    value = std::clamp(value, 0.0, 1.0);
    const auto srgb = value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
    return int(std::lround(srgb * 255));
}
void pixel(const Image& image, Color expected, const std::string& label, int x = extent / 2, int y = extent / 2,
           int tolerance = 3) {
    require(x >= 0 && y >= 0 && x < int(extent) && y < int(extent), "PBR probe lies outside capture");
    for (unsigned channel = 0; channel < 3; ++channel) {
        const auto actual = image.rgb[(std::size_t(y) * extent + x) * 3 + channel];
        require(std::abs(int(actual) - encoded(expected[channel])) <= tolerance,
                label + ": channel " + std::to_string(channel) + " expected " +
                    std::to_string(encoded(expected[channel])) + ", got " + std::to_string(actual));
    }
}
assets::ModelData plane(float metallic = 0, float roughness = 0.5f, math::Vec3 shadingNormal = normal,
                        bool localOrigin = false) {
    assets::ModelData result;
    result.name = "Analytical GGX-Smith-Schlick plane";
    auto& material = result.materials.emplace_back();
    material.baseColor = math::Vec4(base, 1);
    material.metallicFactor = metallic;
    material.roughnessFactor = roughness;
    auto& mesh = result.meshes.emplace_back();
    const float z = localOrigin ? 0.0f : center.z;
    mesh.vertices = {{{-2, -2, z}, shadingNormal, {0, 0}},
                     {{2, -2, z}, shadingNormal, {1, 0}},
                     {{2, 2, z}, shadingNormal, {1, 1}},
                     {{-2, 2, z}, shadingNormal, {0, 1}}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return result;
}
math::Mat4 canonicalProjection(const render::RenderSettings& settings) {
    if (settings.clipSpace.y_down) return settings.viewProjection;
    auto reflection = math::identity();
    reflection.at(1, 1) = -1;
    return reflection * settings.viewProjection;
}
// Intersect the pixel's unprojected ray with z=-3. This references actual sample
// positions rather than assuming a projected world point hits a pixel center.
math::Vec3 worldAt(const render::RenderSettings& settings, int x, int y) {
    const auto inverse = math::inverse(canonicalProjection(settings));
    const float ndcX = 2.0f * (float(x) + 0.5f) / extent - 1;
    const float ndcY = 2.0f * (float(y) + 0.5f) / extent - 1;
    const auto a = math::transform_point(inverse, {ndcX, ndcY, 0});
    const auto b = math::transform_point(inverse, {ndcX, ndcY, 1});
    require(std::isfinite(a.z) && std::isfinite(b.z) && std::abs(b.z - a.z) > 1e-6f,
            "PBR camera ray is parallel to fixture");
    return a + (b - a) * ((center.z - a.z) / (b.z - a.z));
}
void closedFormOracle() {
    // At normal incidence G=1, F=F0 and D=1/(pi*r^4), independent
    // of the Lambda/half-vector evaluator used for the general GPU oracle.
    for (const float metallic : {0.0f, 0.35f, 1.0f})
        for (const float roughness : {0.0f, 0.045f, 0.2f, 0.5f, 1.0f}) {
            const auto actual = tests::pbrReference(base, metallic, roughness, normal, normal, normal, {1, 1, 1});
            const double r = std::max(double(roughness), 0.045);
            for (unsigned channel = 0; channel < 3; ++channel) {
                const double color = base[channel], f0 = 0.04 * (1.0 - metallic) + color * metallic;
                const double diffuse = (1.0 - f0) * (1.0 - metallic) * color / std::numbers::pi;
                const double specular = f0 / (4 * std::numbers::pi * r * r * r * r);
                require(std::abs(actual[channel] - diffuse - specular) < std::max(1e-8, specular * 1e-9),
                        "General PBR oracle disagrees with normal-incidence closed form");
            }
        }
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(320, 240, "AEG PBR lighting test", nullptr, nullptr);
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
        closedFormOracle();
        const std::filesystem::path output(argv[1]);
        std::filesystem::create_directories(output);
        tests::TestRenderer renderer(window, true);
        render::RenderSettings settings;
        settings.background = {0, 0, 0, 1};
        settings.ambient = 0;
        settings.lights[0].intensity = 1;
        settings.lights[0].direction = -normal;
        settings.normalMapping = false;
        auto frame = [&] {
            bool drew{};
            for (unsigned retry = 0; retry < 8 && !drew; ++retry) {
                glfwPollEvents();
                renderer.drawFrame(settings, [&] {
                    renderer.resizeViewport(extent, extent);
                    ImGui::Begin("PBR analytical viewport");
                    ImGui::Image(renderer.viewportTexture(), {128, 128});
                    ImGui::End();
                    drew = true;
                });
            }
            require(drew, "No PBR frame submitted");
        };
        unsigned captures{};
        auto capture = [&] {
            const auto path = output / ("pbr-" + std::to_string(captures++) + ".ppm");
            renderer.captureViewport(path);
            return read(path);
        };
        auto show = [&](const assets::ModelData& model, render::AntialiasingSettings aa = {}) {
            renderer.setAntialiasing(aa);
            renderer.uploadModel(model);
            for (unsigned i = 0; i < (aa.taa ? 8u : 2u); ++i) frame();
            return capture();
        };
        auto setCamera = [&](bool perspective, math::ClipSpaceConvention convention = {true, false},
                             math::Vec3 eye = {0, 0, 0}, math::Vec3 forward = {0, 0, -1}) {
            const auto projection = perspective ? math::perspective(math::to_radians(60), 1, 0.1f, 20, convention)
                                                : math::orthographic(-2, 2, -2, 2, 0.1f, 20, convention);
            settings.set_camera({math::look_at(eye, eye + forward, {0, 1, 0}), projection, convention});
        };
        auto expected = [&](float metallic, float roughness, math::Vec3 n = normal, math::Vec3 towardViewer = normal) {
            return tests::pbrReference(base, metallic, roughness, n, -settings.lights[0].direction, towardViewer,
                                       settings.lights[0].color * settings.lights[0].intensity, settings.ambient);
        };

        setCamera(false);
        // Low roughness highlights are measured below saturation, so removing
        // the floor or changing the roughness remapping cannot hide in white.
        for (const float metallic : {0.0f, 0.35f, 1.0f})
            for (const float roughness : {0.0f, 0.045f, 0.2f, 0.5f, 1.0f}) {
                const double r = std::max(double(roughness), 0.045);
                settings.lights[0].intensity = float(std::min(1.0, 2 * std::numbers::pi * r * r * r * r));
                pixel(show(plane(metallic, roughness)), expected(metallic, roughness),
                      "Normal-incidence metallic=" + std::to_string(metallic) +
                          " roughness=" + std::to_string(roughness));
            }

        settings.lights[0].intensity = 1;
        const auto angledView = math::normalize(math::Vec3{0.6f, 0, 0.8f});
        setCamera(false, {true, false}, center + angledView * 3, -angledView);
        settings.lights[0].direction = -math::normalize(math::Vec3{-0.5f, 0.2f, 1});
        for (const float metallic : {0.0f, 0.45f, 1.0f})
            for (const float roughness : {0.2f, 0.65f, 1.0f})
                pixel(show(plane(metallic, roughness)), expected(metallic, roughness, normal, angledView),
                      "Off-axis GGX-Smith-Schlick");

        const auto grazingView = math::normalize(math::Vec3{1, 0, 0.12f});
        setCamera(false, {true, false}, center + grazingView * 3, -grazingView);
        settings.lights[0].direction = -math::normalize(math::Vec3{-1, 0, 0.2f});
        settings.lights[0].intensity = 0.4f;
        for (const float metallic : {0.0f, 1.0f})
            for (const float roughness : {0.6f, 0.9f})
                pixel(show(plane(metallic, roughness)), expected(metallic, roughness, normal, grazingView),
                      "Grazing-angle Smith masking and Fresnel");

        setCamera(false);
        settings.lights[0].direction = -math::normalize(math::Vec3{0.4f, -0.2f, 1});
        settings.lights[0].color = {0.2f, 0.5f, 0.9f};
        for (const float intensity : {0.0f, 0.25f, 1.5f}) {
            settings.lights[0].intensity = intensity;
            pixel(show(plane(0.3f, 0.7f)), expected(0.3f, 0.7f), "Linear colored directional radiance");
        }
        settings.lights[0].direction = {-1, 0, 0};
        settings.lights[0].intensity = 1;
        pixel(show(plane(0.3f, 0.7f, {1, 0, 0})), {0, 0, 0},
              "Exactly grazing shading normal has no direct contribution");
        settings.lights[0].direction = -normal;
        settings.lights[0].color = math::Vec3{std::numeric_limits<float>::max()};
        settings.lights[0].intensity = std::numeric_limits<float>::max();
        pixel(show(plane(0.0f, 0.045f)), {1, 1, 1}, "Finite extreme radiance saturates without NaN artifacts");
        settings.lights[0].color = {0.2f, 0.5f, 0.9f};
        settings.ambient = 0.3f;
        settings.lights[0].direction = normal; // Directional light behind the surface.
        settings.lights[0].intensity = 3;
        pixel(show(plane(0, 0.5f)), expected(0, 0.5f), "Back light contributes only diffuse ambient");
        pixel(show(plane(1, 0.5f)), {0, 0, 0}, "Metal has neither diffuse ambient nor back-side direct light");
        settings.lights[0].direction = -normal;
        settings.lights[0].intensity = 0;
        pixel(show(plane(0.4f, 0.5f)), expected(0.4f, 0.5f), "Zero light keeps only ambient diffuse");
        settings.ambient = 0;
        settings.lights[0].color = {1, 1, 1};
        settings.lights[0].intensity = 1;

        // Orthographic rays stay parallel as the eye translates. A position-
        // based view vector would introduce an incorrect highlight change here.
        settings.lights[0].direction = -math::normalize(math::Vec3{0.4f, 0, 1});
        const auto orthographic = show(plane(0.7f, 0.55f));
        setCamera(false, {true, false}, {0.7f, 0, 0});
        const auto translatedOrthographic = show(plane(0.7f, 0.55f));
        pixel(orthographic, expected(0.7f, 0.55f), "Orthographic base camera");
        pixel(translatedOrthographic, expected(0.7f, 0.55f), "Orthographic translated camera");

        // Perspective views use the actual fragment's WORLD position. Probe
        // separated points and a moved camera rather than only the optical axis.
        settings.lights[0].direction = -normal;
        for (const float cameraX : {0.0f, 0.8f}) {
            setCamera(true, {true, false}, {cameraX, 0, 0});
            const auto image = show(plane(0.7f, 0.4f));
            for (const int x : {int(extent / 3), int(extent / 2), int(extent * 2 / 3)}) {
                const auto point = worldAt(settings, x, extent / 2);
                const auto view = settings.cameraPosition - point;
                pixel(image, expected(0.7f, 0.4f, normal, view), "Perspective fragment view direction", x);
            }
        }

        // Nonuniform model scale needs inverse-transpose normals, and the
        // transformed world position must still match the perspective eye.
        setCamera(true);
        const auto localNormal = math::normalize(math::Vec3{0.4f, 0.3f, 1});
        constexpr float angle = 0.35f;
        // Shift the local origin off the optical axis. Accidentally passing
        // local positions to the BRDF then produces a different view vector,
        // rather than being hidden by the zero-vector fallback at local origin.
        settings.model =
            math::translation({0.45f, 0, -3}) * math::rotation({0, 1, 0}, angle) * math::scale({1.7f, 0.6f, 0.8f});
        const auto scaled = math::Vec3{localNormal.x / 1.7f, localNormal.y / 0.6f, localNormal.z / 0.8f};
        const auto transformedNormal =
            math::normalize(math::Vec3{std::cos(angle) * scaled.x + std::sin(angle) * scaled.z, scaled.y,
                                       -std::sin(angle) * scaled.x + std::cos(angle) * scaled.z});
        settings.lights[0].direction = -math::normalize(math::Vec3{-0.3f, 0.2f, 1});
        pixel(show(plane(0.25f, 0.65f, localNormal, true)), expected(0.25f, 0.65f, transformedNormal),
              "Model translation, rotation and inverse-transpose normal");
        settings.model = math::identity();

        std::vector<render::AntialiasingSettings> modes{{}, {1, true, false}};
        if (renderer.supportedMsaaSamples() & 4u) modes.push_back({4, true, false});
        if (renderer.supportsTemporalAA()) modes.push_back({1, true, true});
        settings.lights[0].intensity = 0.8f;
        settings.lights[0].color = {0.8f, 0.9f, 1};
        for (const auto aa : modes)
            for (const bool perspective : {false, true})
                for (const bool yDown : {false, true})
                    for (const bool reversed : {false, true}) {
                        setCamera(perspective, {yDown, reversed});
                        pixel(show(plane(0.4f, 0.65f), aa), expected(0.4f, 0.65f),
                              "PBR AA / projection / clip convention");
                    }

        if (renderer.supportsTemporalAA()) {
            setCamera(true);
            renderer.setAntialiasing({1, false, true});
            renderer.uploadModel(plane(0.2f, 0.6f));
            for (unsigned i = 0; i < 4; ++i) frame();
            require(renderer.temporalFrameCount() >= 3, "PBR history failed to accumulate");
            settings.lights[0].color = {0.5f, 0.75f, 1};
            frame();
            require(renderer.temporalFrameCount() == 1, "Light color change retained stale PBR history");
            frame();
            settings.lights[0].intensity = 0.3f;
            frame();
            require(renderer.temporalFrameCount() == 1, "Light intensity change retained stale PBR history");
            setCamera(true, {true, false}, {0.03f, 0, 0});
            frame();
            require(renderer.temporalFrameCount() == 2, "Ordinary camera movement reset PBR history");
            settings.model = math::translation({0.02f, 0, 0});
            frame();
            require(renderer.temporalFrameCount() == 3, "Ordinary model movement reset PBR history");
        }
        renderer.waitIdle();
        require(renderer.validationErrorCount() == 0, "Vulkan validation rejected the PBR render path");
        require(diagnostics.str().find("Vulkan validation error") == std::string::npos,
                "PBR diagnostics contain a Vulkan validation error");
        std::cout << "PBR lighting: " << captures << " analytical captures; GGX, correlated Smith, Schlick, camera, "
                  << "material, AA and clip conventions; Vulkan errors: 0\n";
    } catch (const std::exception& error) {
        std::cout << "PBR lighting test failed: " << error.what() << '\n';
        result = 1;
    }
    std::cerr.rdbuf(previous);
    if (result) std::cerr << diagnostics.str();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
