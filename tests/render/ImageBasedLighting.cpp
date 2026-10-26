#include "RendererHarness.hpp"
#include "PbrReference.hpp"
#include <EnvironmentImage.hpp>
#include <RenderCamera.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
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
constexpr math::Vec3 base{0.7f, 0.3f, 0.1f}, normal{0, 0, 1}, center{0, 0, -3};
constexpr double pi = std::numbers::pi;
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
            "Invalid IBL capture header");
    Image result{std::vector<unsigned char>(extent * extent * 3)};
    input.read(reinterpret_cast<char*>(result.rgb.data()), std::streamsize(result.rgb.size()));
    require(input.gcount() == std::streamsize(result.rgb.size()) && input.peek() == std::char_traits<char>::eof(),
            "IBL capture has an incorrect byte count");
    return result;
}
int encoded(double value) {
    require(std::isfinite(value), "Nonfinite IBL reference");
    value = std::clamp(value, 0.0, 1.0);
    const auto srgb = value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
    return int(std::lround(srgb * 255));
}
void pixel(const Image& image, Color expected, const std::string& label, int x = extent / 2, int y = extent / 2,
           int tolerance = 3) {
    for (unsigned c = 0; c < 3; ++c) {
        const auto actual = image.rgb[(std::size_t(y) * extent + x) * 3 + c];
        require(std::abs(int(actual) - encoded(expected[c])) <= tolerance,
                label + ": channel=" + std::to_string(c) + " expected=" + std::to_string(encoded(expected[c])) +
                    " actual=" + std::to_string(actual));
    }
}
void same(const Image& actual, const Image& expected, const std::string& label) {
    require(actual.rgb == expected.rgb, label + " changed captured pixels");
}
assets::ModelData plane(float metallic = 0, float roughness = 0.5f, math::Vec3 n = normal) {
    assets::ModelData model;
    model.name = "Independent split-sum IBL plane";
    auto& material = model.materials.emplace_back();
    material.baseColor = math::Vec4(base, 1);
    material.metallicFactor = metallic;
    material.roughnessFactor = roughness;
    auto& mesh = model.meshes.emplace_back();
    mesh.vertices = {
        {{-2, -2, -3}, n, {0, 0}}, {{2, -2, -3}, n, {1, 0}}, {{2, 2, -3}, n, {1, 1}}, {{-2, 2, -3}, n, {0, 1}}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return model;
}
constexpr Color bias{0.35, 0.4, 0.35}, amplitude{0.25, 0.2, 0.3};
assets::HdrImageData environment(bool directional = false) {
    assets::HdrImageData image{256, 128, std::vector<float>(256 * 128 * 4)};
    for (unsigned y = 0; y < image.height; ++y)
        for (unsigned x = 0; x < image.width; ++x) {
            const double theta = pi * (double(y) + 0.5) / image.height;
            const double phi = 2 * pi * ((double(x) + 0.5) / image.width - 0.5);
            const Color d{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
            const auto p = (std::size_t(y) * image.width + x) * 4;
            for (unsigned c = 0; c < 3; ++c) image.rgba[p + c] = float(directional ? bias[c] + amplitude[c] * d[c] : 1);
            image.rgba[p + 3] = 1;
        }
    return image;
}

// Independent direct integration over incoming light directions. The renderer
// uses a preintegrated LUT and importance-sampled GGX half vectors; this oracle
// uses neither. The underlying BRDF matches PbrReference's double Lambda form.
std::array<double, 2> dfg(double noV, double roughness) {
    constexpr unsigned muSteps = 384, phiSteps = 768;
    const double vx = std::sqrt(std::max(0.0, 1 - noV * noV)), a2 = std::pow(roughness, 4);
    const auto lambda = [a2](double mu) { return (std::sqrt(1 + a2 * (1 - mu * mu) / (mu * mu)) - 1) * 0.5; };
    std::array<double, 2> result{};
    for (unsigned m = 0; m < muSteps; ++m) {
        const double noL = (double(m) + 0.5) / muSteps;
        const double radius = std::sqrt(1 - noL * noL), g = 1 / (1 + lambda(noL) + lambda(noV));
        for (unsigned p = 0; p < phiSteps; ++p) {
            const double phi = 2 * pi * (double(p) + 0.5) / phiSteps;
            const double hx = vx + radius * std::cos(phi), hy = radius * std::sin(phi), hz = noV + noL;
            const double length = std::sqrt(hx * hx + hy * hy + hz * hz);
            const double noH = hz / length, voH = (vx * hx + noV * hz) / length;
            const double d = a2 / (pi * std::pow(1 + noH * noH * (a2 - 1), 2));
            const double fc = std::pow(std::clamp(1 - voH, 0.0, 1.0), 5);
            const double weight = d * g / (4 * noV);
            result[0] += weight * (1 - fc);
            result[1] += weight * fc;
        }
    }
    for (auto& component : result) component *= 2 * pi / (double(muSteps) * phiSteps);
    return result;
}
Color furnace(float metallic, float roughness, math::Vec3 n = normal, math::Vec3 viewer = normal,
              double intensity = 1) {
    const double noV = std::max(0.00001, double(math::dot(math::normalize(n), math::normalize(viewer))));
    const double r = std::clamp(double(roughness), 0.045, 1.0);
    const auto ab = dfg(noV, r);
    Color result{};
    for (unsigned c = 0; c < 3; ++c) {
        const double f0 = 0.04 * (1 - metallic) + double(base[c]) * metallic;
        // Deliberate split-sum diffuse approximation: Frough uses NoV, not
        // the half-vector Fresnel in the exact hemisphere BRDF integral.
        const double fRough = f0 + (std::max(1 - r, f0) - f0) * std::pow(1 - noV, 5);
        const double diffuse = (1 - fRough) * (1 - metallic) * double(base[c]); // E=pi, divided once by pi.
        result[c] = intensity * (diffuse + f0 * ab[0] + ab[1]);
    }
    return result;
}
math::Vec3 worldAt(const render::RenderSettings& settings, int x, int y) {
    auto projection = settings.viewProjection;
    if (!settings.clipSpace.y_down) {
        auto reflection = math::identity();
        reflection.at(1, 1) = -1;
        projection = reflection * projection;
    }
    const auto inverse = math::inverse(projection);
    const float nx = 2 * (float(x) + 0.5f) / extent - 1, ny = 2 * (float(y) + 0.5f) / extent - 1;
    const auto a = math::transform_point(inverse, {nx, ny, 0});
    const auto b = math::transform_point(inverse, {nx, ny, 1});
    return a + (b - a) * ((center.z - a.z) / (b.z - a.z));
}
Color sharpDirectional(math::Vec3 n, math::Vec3 view, double rotation = 0, double intensity = 1) {
    n = math::normalize(n);
    view = math::normalize(view);
    const double noV = std::clamp(double(math::dot(n, view)), 0.0, 1.0);
    const auto reflected = n * float(2 * noV) - view;
    const Color d{std::cos(rotation) * reflected.x - std::sin(rotation) * reflected.z, reflected.y,
                  std::sin(rotation) * reflected.x + std::cos(rotation) * reflected.z};
    Color result{};
    for (unsigned c = 0; c < 3; ++c) {
        const double fresnel = double(base[c]) + (1 - double(base[c])) * std::pow(1 - noV, 5);
        result[c] = intensity * (bias[c] + amplitude[c] * d[c]) * fresnel;
    }
    return result;
}
Color roughDirectional(float metallic, math::Vec3 n = normal, math::Vec3 color = base) {
    n = math::normalize(n);
    const double noV = n.z;
    const auto reflected = n * float(2 * noV) - normal;
    const auto ab = dfg(noV, 1);
    Color result{};
    for (unsigned c = 0; c < 3; ++c) {
        const double f0 = 0.04 * (1 - metallic) + double(color[c]) * metallic;
        // At r=1 the prefilter's N=V GGX density is uniform in L;
        // NoL normalization gives the same 2/3 first-order moment as E/pi.
        const double diffuseLi = bias[c] + 2.0 / 3 * amplitude[c] * n[c];
        const double specularLi = bias[c] + 2.0 / 3 * amplitude[c] * reflected[c];
        result[c] = (1 - f0) * (1 - metallic) * color[c] * diffuseLi + (f0 * ab[0] + ab[1]) * specularLi;
    }
    return result;
}
}

int main(int argc, char** argv) {
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(320, 240, "AEG integrated image based lighting test", nullptr, nullptr);
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
        auto& api = renderer.renderer();
        require(api.supportsEnvironment(), "Test GPU does not support integrated IBL");
        require(!api.environment().enabled && !api.hasEnvironment(), "Fresh renderer does not preserve disabled IBL");
        render::RenderSettings settings;
        settings.background = {0, 0, 0, 1};
        settings.ambient = 0.23f;
        settings.lightCount = 0;
        settings.normalMapping = false;
        auto setCamera = [&](bool perspective, math::ClipSpaceConvention convention = {true, false},
                             math::Vec3 eye = {0, 0, 0}, math::Vec3 forward = {0, 0, -1}) {
            const auto projection = perspective ? math::perspective(math::to_radians(60), 1, 0.1f, 20, convention)
                                                : math::orthographic(-2, 2, -2, 2, 0.1f, 20, convention);
            settings.set_camera({math::look_at(eye, eye + forward, {0, 1, 0}), projection, convention});
        };
        auto frame = [&] {
            bool drew{};
            for (unsigned retry = 0; retry < 8 && !drew; ++retry) {
                glfwPollEvents();
                renderer.drawFrame(settings, [&] {
                    renderer.resizeViewport(extent, extent);
                    ImGui::Begin("IBL analytical viewport");
                    ImGui::Image(renderer.viewportTexture(), {128, 128});
                    ImGui::End();
                    drew = true;
                });
            }
            require(drew, "No IBL frame submitted");
        };
        unsigned captures{};
        auto capture = [&] {
            const auto path = output / ("ibl-" + std::to_string(captures++) + ".ppm");
            renderer.captureViewport(path);
            return read(path);
        };
        auto settle = [&](unsigned count = 2) {
            for (unsigned i = 0; i < count; ++i) frame();
        };
        auto show = [&](const assets::ModelData& model, render::AntialiasingSettings aa = {}) {
            renderer.setAntialiasing(aa);
            renderer.uploadModel(model);
            settle(aa.taa ? 8u : 2u);
            return capture();
        };

        setCamera(false);
        const auto legacy = show(plane(0.3f, 0.65f));
        const auto legacyReference =
            tests::pbrReference(base, 0.3f, 0.65f, normal, normal, normal, {}, settings.ambient);
        pixel(legacy, legacyReference, "Default-disabled legacy ambient");
        api.uploadEnvironment(environment());
        require(api.hasEnvironment() && !api.environment().enabled, "Environment upload changed enable state");
        settle();
        same(capture(), legacy, "Disabled environment upload");

        api.setEnvironment({true, 1, 0});
        // No directional lights and nonzero legacy ambient: these values can
        // only arise from IBL replacing ambient, with diffuse E/pi exactly once.
        for (const float metallic : {0.0f, 0.4f, 1.0f})
            for (const float roughness : {0.35f, 0.65f, 1.0f})
                pixel(show(plane(metallic, roughness)), furnace(metallic, roughness),
                      "White furnace metallic=" + std::to_string(metallic) + " roughness=" + std::to_string(roughness));
        const auto angled = math::normalize(math::Vec3{0.6f, 0, 0.8f});
        setCamera(false, {true, false}, center + angled * 3, -angled);
        for (const float metallic : {0.0f, 1.0f})
            pixel(show(plane(metallic, 0.65f)), furnace(metallic, 0.65f, normal, angled),
                  "Off-axis furnace NoV and rough Fresnel");

        setCamera(false);
        for (const float intensity : {0.0f, 0.25f, 0.8f}) {
            api.setEnvironment({true, intensity, 0});
            pixel(show(plane(0.4f, 0.65f)), furnace(0.4f, 0.65f, normal, normal, intensity),
                  "Environment linear intensity applied once");
        }
        api.setEnvironment({false, 0.8f, 0});
        same(show(plane(0.3f, 0.65f)), legacy, "Disabling IBL restores legacy pixels");
        api.setEnvironment({true, 1, 0});
        settings.lightCount = 1;
        settings.lights[0].direction = -normal;
        settings.lights[0].intensity = 0.2f;
        settings.lights[0].color = {0.7f, 0.8f, 1};
        auto combined = furnace(0.4f, 0.65f);
        const auto direct = tests::pbrReference(base, 0.4f, 0.65f, normal, normal, normal,
                                                settings.lights[0].color * settings.lights[0].intensity, 0);
        for (unsigned c = 0; c < 3; ++c) combined[c] += direct[c];
        pixel(show(plane(0.4f, 0.65f)), combined, "IBL adds to unchanged analytical direct PBR");
        settings.lightCount = 0;

        std::vector<render::AntialiasingSettings> modes{{}, {1, true, false}};
        if (renderer.supportedMsaaSamples() & 4u) modes.push_back({4, true, false});
        if (renderer.supportsTemporalAA()) modes.push_back({1, true, true});
        const auto matrixReference = furnace(0.4f, 0.65f);
        for (const auto aa : modes)
            for (const bool perspective : {false, true})
                for (const bool yDown : {false, true})
                    for (const bool reversed : {false, true}) {
                        setCamera(perspective, {yDown, reversed});
                        pixel(show(plane(0.4f, 0.65f), aa), matrixReference,
                              "IBL projection / clip / antialiasing convention");
                    }
        renderer.setAntialiasing({});
        setCamera(true);
        const auto perspective = show(plane(0.7f, 0.55f));
        for (const int x : {int(extent / 3), int(extent / 2), int(extent * 2 / 3)}) {
            const auto view = settings.cameraPosition - worldAt(settings, x, extent / 2);
            pixel(perspective, furnace(0.7f, 0.55f, normal, view), "Perspective fragment IBL NoV", x);
        }
        setCamera(false);
        const auto ortho = show(plane(0.7f, 0.55f));
        setCamera(false, {true, false}, {0.7f, 0, 0});
        pixel(show(plane(0.7f, 0.55f)), furnace(0.7f, 0.55f), "Translated orthographic parallel IBL rays");
        pixel(ortho, furnace(0.7f, 0.55f), "Orthographic IBL baseline");

        api.uploadEnvironment(environment(true));
        setCamera(false);
        api.setEnvironment({true, 1, 0});
        const auto sharp = show(plane(1, 0.045f));
        pixel(sharp, sharpDirectional(normal, normal), "Directional environment world reflection");
        api.setEnvironment({true, 1, float(pi / 2)});
        pixel(show(plane(1, 0.045f)), sharpDirectional(normal, normal, pi / 2),
              "Positive Y environment rotation samples inverse rotation");
        api.setEnvironment({true, 1, 0});
        setCamera(false, {true, false}, center + angled * 3, -angled);
        pixel(show(plane(1, 0.045f)), sharpDirectional(normal, angled), "Environment reflection follows camera");
        setCamera(false);
        const auto rough = show(plane(1, 1));
        require(int(sharp.rgb[(extent / 2 * extent + extent / 2) * 3]) -
                        int(rough.rgb[(extent / 2 * extent + extent / 2) * 3]) >
                    20,
                "Runtime roughness did not change the prefiltered response");
        auto brightRoughMetal = plane(1, 1);
        brightRoughMetal.materials[0].baseColor = {0.7f, 0.7f, 0.7f, 1};
        pixel(show(brightRoughMetal), roughDirectional(1, normal, {0.7f, 0.7f, 0.7f}),
              "Roughness selects last prefilter mip with analytic first-order moment");
        const auto tiltedNormal = math::normalize(math::Vec3{0.6f, 0, 0.8f});
        pixel(show(plane(0, 1, tiltedNormal)), roughDirectional(0, tiltedNormal),
              "Directional dielectric irradiance and reflected specular use world normal");

        auto mapped = plane(1, 0.045f);
        mapped.materials[0].hasNormalMap = true;
        mapped.materials[0].normalTexture = {1, 1, {166, 128, 249, 255}};
        for (auto& vertex : mapped.meshes[0].vertices) vertex.tangent = {1, 0, 0, 1};
        const auto mappedNormal =
            math::normalize(math::Vec3{2.0f * 166 / 255 - 1, 2.0f * 128 / 255 - 1, 2.0f * 249 / 255 - 1});
        settings.normalMapping = true;
        pixel(show(mapped), sharpDirectional(mappedNormal, normal), "IBL uses final tangent-space mapped normal");
        settings.normalMapping = false;
        same(show(mapped), sharp, "Disabling normal mapping restores IBL vertex normal");
        settings.unlit = true;
        const auto unlit = show(mapped);
        pixel(unlit, {base.x, base.y, base.z}, "Unlit base-color bypass");
        api.setEnvironment({true, 0, float(pi)});
        settings.normalMapping = true;
        same(show(mapped), unlit, "Unlit ignores environment intensity/rotation and normal map");
        settings.unlit = false;
        settings.normalMapping = false;
        api.setEnvironment({true, 1, 0});

        if (renderer.supportsTemporalAA()) {
            setCamera(true);
            renderer.setAntialiasing({1, false, true});
            renderer.uploadModel(plane(1, 0.65f));
            settle(4);
            require(renderer.temporalFrameCount() >= 3, "IBL history did not accumulate");
            api.setEnvironment(api.environment());
            frame();
            require(renderer.temporalFrameCount() >= 4, "Identical environment settings reset history");
            api.setEnvironment({true, 0.5f, 0});
            frame();
            require(renderer.temporalFrameCount() == 1, "Environment intensity retained stale history");
            frame();
            api.setEnvironment({true, 0.5f, 0.3f});
            frame();
            require(renderer.temporalFrameCount() == 1, "Environment rotation retained stale history");
            frame();
            api.setEnvironment({false, 0.5f, 0.3f});
            frame();
            require(renderer.temporalFrameCount() == 1, "Environment enable retained stale history");
            api.setEnvironment({true, 1, 0});
            settle(3);
            const auto beforeInvalid = api.environment();
            const auto history = renderer.temporalFrameCount();
            auto invalid = environment();
            invalid.rgba[0] = -1;
            bool rejected{};
            try {
                api.uploadEnvironment(invalid);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected && api.environment() == beforeInvalid && api.hasEnvironment() &&
                        renderer.temporalFrameCount() == history,
                    "Rejected HDR replacement changed live IBL/history");
            frame();
            const auto beforeMovement = renderer.temporalFrameCount();
            setCamera(true, {true, false}, {0.03f, 0, 0});
            frame();
            require(renderer.temporalFrameCount() == beforeMovement + 1, "Ordinary camera motion reset IBL history");
            api.uploadEnvironment(environment());
            frame();
            require(renderer.temporalFrameCount() == 1, "Successful environment replacement retained stale history");
        }
        renderer.waitIdle();
        require(renderer.validationErrorCount() == 0, "Vulkan validation rejected integrated IBL");
        require(diagnostics.str().find("Vulkan validation error") == std::string::npos,
                "Integrated IBL diagnostics contain a Vulkan validation error");
        std::cout << "Image based lighting: " << captures
                  << " captures; legacy defaults, white furnace, dielectric "
                     "diffuse/specular, direct PBR, camera/rotation/intensity, normal maps, unlit, AA/clip and TAA; "
                     "Vulkan errors: 0\n";
    } catch (const std::exception& error) {
        std::cout << "Image based lighting test failed: " << error.what() << '\n';
        result = 1;
    }
    std::cerr.rdbuf(previous);
    if (result) std::cerr << diagnostics.str();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
