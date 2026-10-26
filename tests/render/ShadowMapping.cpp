#include "RendererHarness.hpp"
#include "PbrReference.hpp"
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
#include <string>
#include <vector>

namespace {
using namespace aeg;
using Color = std::array<double, 3>;
constexpr unsigned extent = 257;
constexpr math::Vec3 base{0.4f, 0.4f, 0.4f}, normal{0, 0, 1};
constexpr float roughness = 0.8f;
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
            "Invalid shadow capture");
    Image image{std::vector<unsigned char>(extent * extent * 3)};
    input.read(reinterpret_cast<char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    require(input.gcount() == static_cast<std::streamsize>(image.rgb.size()), "Truncated shadow capture");
    return image;
}
int encoded(double linear) {
    require(std::isfinite(linear), "Shadow PBR reference is nonfinite");
    // Capture encodes the already-quantized linear RGBA8 scene/output texture.
    const auto value = std::round(std::clamp(linear, 0.0, 1.0) * 255) / 255;
    return int(std::lround(255 * (value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - 0.055)));
}
math::Mat4 canonical(const render::RenderSettings& settings) {
    if (settings.clipSpace.y_down) return settings.viewProjection;
    auto reflection = math::identity();
    reflection.at(1, 1) = -1;
    return reflection * settings.viewProjection;
}
std::array<int, 2> screen(const render::RenderSettings& settings, math::Vec3 world) {
    const auto ndc = math::transform_point(canonical(settings), world);
    const std::array<int, 2> result{int(std::lround((ndc.x + 1) * extent * 0.5f - 0.5f)),
                                    int(std::lround((ndc.y + 1) * extent * 0.5f - 0.5f))};
    require(result[0] >= 0 && result[1] >= 0 && result[0] < int(extent) && result[1] < int(extent),
            "Shadow probe outside viewport");
    return result;
}
math::Vec3 worldAt(const render::RenderSettings& settings, std::array<int, 2> pixel, float z) {
    const auto inverse = math::inverse(canonical(settings));
    const float x = (float(pixel[0]) + 0.5f) * 2 / extent - 1, y = (float(pixel[1]) + 0.5f) * 2 / extent - 1;
    const auto near = math::transform_point(inverse, {x, y, 0});
    const auto far = math::transform_point(inverse, {x, y, 1});
    return near + (far - near) * ((z - near.z) / (far.z - near.z));
}
void pixel(const Image& image, std::array<int, 2> at, Color expected, const std::string& label, int tolerance = 4) {
    for (unsigned c = 0; c < 3; ++c) {
        const auto actual = image.rgb[(std::size_t(at[1]) * extent + at[0]) * 3 + c];
        require(std::abs(int(actual) - encoded(expected[c])) <= tolerance,
                label + " channel " + std::to_string(c) + " expected " + std::to_string(encoded(expected[c])) +
                    ", got " + std::to_string(actual));
    }
}
assets::ModelData fixture(assets::AlphaMode casterMode = assets::AlphaMode::Opaque, bool maskHole = false,
                          bool blendedReceiver = false) {
    assets::ModelData model;
    model.name = "One model: offset caster and analytic receiver; bounds from vertices";
    auto& receiver = model.materials.emplace_back();
    receiver.baseColor = math::Vec4(base, blendedReceiver ? 0.7f : 1);
    receiver.alphaMode = blendedReceiver ? assets::AlphaMode::Blend : assets::AlphaMode::Opaque;
    receiver.roughnessFactor = roughness;
    auto& caster = model.materials.emplace_back();
    caster.baseColor = {0.4f, 0.4f, 0.4f, 0.65f};
    caster.alphaMode = casterMode;
    caster.alphaCutoff = 0.5f;
    caster.roughnessFactor = roughness;
    if (maskHole) {
        caster.hasTexture = true;
        caster.texture.width = 8;
        caster.texture.rgba.resize(8 * 4, 255);
        for (unsigned x = 0; x < 4; ++x) caster.texture.rgba[x * 4 + 3] = 0;
    }
    auto& rear = model.meshes.emplace_back();
    rear.vertices = {{{-2, -2, -4}, normal, {0, 0}},
                     {{2, -2, -4}, normal, {1, 0}},
                     {{2, 2, -4}, normal, {1, 1}},
                     {{-2, 2, -4}, normal, {0, 1}}};
    rear.indices = {0, 1, 2, 0, 2, 3};
    auto& front = model.meshes.emplace_back();
    front.materialIndex = 1;
    front.vertices = {{{-0.9f, -0.5f, -2.5f}, normal, {0, 0}},
                      {{-0.3f, -0.5f, -2.5f}, normal, {1, 0}},
                      {{-0.3f, 0.5f, -2.5f}, normal, {1, 1}},
                      {{-0.9f, 0.5f, -2.5f}, normal, {0, 1}}};
    front.indices = {0, 1, 2, 0, 2, 3};
    return model;
}
// Only primary direct radiance is multiplied by visibility. The green second
// light and one ambient term provide independent probes against over-shadowing.
Color expected(const render::RenderSettings& settings, math::Vec3 point, double visibility, float opacity = 1,
               unsigned selectedLight = 0) {
    Color result{};
    const bool perspective = settings.viewProjection.at(3, 0) != 0 || settings.viewProjection.at(3, 1) != 0 ||
                             settings.viewProjection.at(3, 2) != 0;
    const auto view = perspective ? settings.cameraPosition - point : settings.cameraTowardViewer;
    for (unsigned c = 0; c < 3; ++c) result[c] = base[c] * settings.ambient;
    for (unsigned i = 0; i < settings.lightCount; ++i) {
        const auto& light = settings.lights[i];
        auto toward = -light.direction;
        double attenuation = 1;
        if (light.type == render::LightType::Spot) {
            toward = light.position - point;
            const double distance =
                std::sqrt(double(toward.x) * toward.x + double(toward.y) * toward.y + double(toward.z) * toward.z);
            const double cutoff = std::max(1 - std::pow(distance / light.range, 4), 0.0);
            attenuation = cutoff * cutoff / std::max(distance * distance, 0.0001);
            const double cosine = math::dot(-math::normalize(toward), math::normalize(light.direction));
            const double inner = std::cos(double(light.innerConeAngle)), outer = std::cos(double(light.outerConeAngle));
            const double t = std::clamp((cosine - outer) / (inner - outer), 0.0, 1.0);
            attenuation *= t * t * (3 - 2 * t);
        }
        const auto direct = tests::pbrReference(base, 0, roughness, normal, toward, view,
                                                light.color * float(light.intensity * attenuation));
        for (unsigned c = 0; c < 3; ++c) result[c] += direct[c] * (i == selectedLight ? visibility : 1);
    }
    for (auto& c : result) c *= opacity;
    return result;
}
}

int main(int argc, char** argv) {
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(320, 240, "AEG shadow-map test", nullptr, nullptr);
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
        require(renderer.supportsShadows(), "GPU lacks required sampled-depth shadow path");
        render::RenderSettings settings;
        settings.background = {0, 0, 0, 1};
        settings.ambient = 0.12f;
        settings.normalMapping = false;
        settings.lightCount = 2;
        auto& primary = settings.lights[0];
        primary.direction = math::normalize(math::Vec3{0.6f, 0, -1});
        primary.color = {1, 0, 0};
        primary.intensity = 1;
        settings.lights[1].direction = {0, 0, -1};
        settings.lights[1].color = {0, 1, 0};
        settings.lights[1].intensity = 0.65f;
        const auto camera = [&](bool perspective = false, math::ClipSpaceConvention clip = {true, false},
                                math::Vec3 eye = {}) {
            settings.set_camera({math::translation(-eye),
                                 perspective ? math::perspective(1, 1, 0.1f, 30, clip)
                                             : math::orthographic(-2, 2, -2, 2, 0.1f, 30, clip),
                                 clip});
        };
        camera();
        render::ShadowSettings shadow;
        shadow.cascadeCount = 1;
        shadow.resolution = std::min(256u, renderer.maximumShadowResolution());
        renderer.uploadModel(fixture());
        const auto frame = [&] {
            bool drew{};
            for (unsigned retry = 0; retry < 8 && !drew; ++retry) {
                glfwPollEvents();
                renderer.drawFrame(settings, [&] {
                    renderer.resizeViewport(extent, extent);
                    ImGui::Begin("Shadow analytic viewport");
                    ImGui::Image(renderer.viewportTexture(), {256, 256});
                    ImGui::End();
                    drew = true;
                });
            }
            require(drew, "No shadow frame submitted");
        };
        unsigned captures{};
        const auto capture = [&] {
            for (unsigned i = 0; i < (renderer.antialiasing().taa ? 8u : 2u); ++i) frame();
            const auto path = output / ("shadow-" + std::to_string(captures++) + ".ppm");
            renderer.captureViewport(path);
            return read(path);
        };
        const auto probe = [&](const Image& image, math::Vec3 target, double visibility, const std::string& label,
                               float opacity = 1, unsigned selectedLight = 0) {
            const auto at = screen(settings, target);
            pixel(image, at, expected(settings, worldAt(settings, at, target.z), visibility, opacity, selectedLight),
                  label);
        };
        const math::Vec3 blocked{0.3f, 0, -4}, lit{1.3f, 0, -4};
        probe(capture(), blocked, 1, "Disabled shadows keep selected direct light");
        shadow.enabled = true;
        renderer.setShadows(shadow);
        const auto opaque = capture();
        probe(opaque, blocked, 0, "Opaque occlusion keeps ambient and unselected colored light");
        probe(opaque, lit, 1, "Unoccluded receiver retains direct light");
        std::swap(settings.lights[0], settings.lights[1]);
        shadow.lightIndex = 1;
        renderer.setShadows(shadow);
        const auto selectedSecond = capture();
        probe(selectedSecond, blocked, 0, "Selected light index one shadows only its own direct radiance", 1, 1);
        probe(selectedSecond, lit, 1, "Selected light index one leaves lit receiver unchanged", 1, 1);
        std::swap(settings.lights[0], settings.lights[1]);
        shadow.lightIndex = 0;
        renderer.setShadows(shadow);
        // Removing the shadow lookup must fail the blocked prediction above;
        // globally dimming direct or ambient must fail this unoccluded probe.
        renderer.uploadModel(fixture(assets::AlphaMode::Mask, true));
        const auto masked = capture();
        probe(masked, {0.15f, 0, -4}, 1, "Mask texture hole transmits selected light");
        probe(masked, {0.45f, 0, -4}, 0, "Mask opaque texels still cast");
        renderer.uploadModel(fixture(assets::AlphaMode::Blend));
        probe(capture(), blocked, 1, "Blended material does not cast a shadow");
        renderer.uploadModel(fixture(assets::AlphaMode::Opaque, false, true));
        probe(capture(), blocked, 0, "Blended receiver receives a shadow", 0.7f);
        renderer.uploadModel(fixture());
        // Coarse map makes each 3x3 nearest PCF step cover several receiver
        // pixels. Require a filtered interior level along the known shadow edge.
        shadow.resolution = 64;
        renderer.setShadows(shadow);
        const auto filtered = capture();
        const auto dark = encoded(expected(settings, blocked, 0)[0]),
                   bright = encoded(expected(settings, blocked, 1)[0]);
        unsigned intermediate{};
        for (float x = -0.1f; x < 0.75f; x += 0.01f) {
            const auto at = screen(settings, {x, 0, -4});
            const auto red = filtered.rgb[(std::size_t(at[1]) * extent + at[0]) * 3];
            if (int(red) > dark + 4 && int(red) < bright - 4) ++intermediate;
        }
        require(intermediate > 0, "Shadow edge lacks intermediate 3x3 PCF visibility");
        shadow.resolution = std::min(256u, renderer.maximumShadowResolution());
        renderer.setShadows(shadow);
        primary.type = render::LightType::Spot;
        primary.position = {-2.1f, 0, 0};
        primary.range = 12;
        primary.intensity = 25;
        primary.innerConeAngle = math::to_radians(30);
        primary.outerConeAngle = math::to_radians(45);
        const auto spot = capture();
        probe(spot, blocked, 0, "Spot shadow occlusion");
        probe(spot, lit, 1, "Spot lit receiver and attenuation");
        primary.type = render::LightType::Directional;
        primary.intensity = 1;
        std::vector<render::AntialiasingSettings> modes{{}, {1, true, false}};
        if (renderer.supportedMsaaSamples() & 4u) modes.push_back({4, true, false});
        if (renderer.supportsTemporalAA()) modes.push_back({1, true, true});
        for (const auto aa : modes) {
            renderer.setAntialiasing(aa);
            for (const bool perspective : {false, true})
                for (const bool down : {false, true})
                    for (const bool reversed : {false, true}) {
                        camera(perspective, {down, reversed});
                        const auto image = capture();
                        probe(image, blocked, 0, "Shadow AA/projection/clip occlusion");
                        probe(image, lit, 1, "Shadow AA/projection/clip lit region");
                    }
        }
        renderer.setAntialiasing({});
        camera(false, {true, false}, {0.2f, 0.1f, 0});
        settings.model =
            math::translation({0.2f, 0.15f, -0.2f}) * math::rotation({0, 0, 1}, 0.15f) * math::scale({1.1f, 0.8f, 1});
        const auto casterCenter = math::transform_point(settings.model, {-0.6f, 0, -2.5f});
        const auto movedShadow = casterCenter + math::Vec3{0.9f, 0, -1.5f};
        const auto moved = capture();
        probe(moved, movedShadow, 0, "Camera/model motion updates the light-space shadow fit");
        probe(moved, math::transform_point(settings.model, lit), 1, "Moved receiver remains lit outside occluder");
        if (renderer.supportsTemporalAA()) {
            renderer.setAntialiasing({1, false, true});
            frame();
            frame();
            require(renderer.temporalFrameCount() >= 2, "Shadow TAA did not accumulate");
            shadow.depthBias += 0.0001f;
            renderer.setShadows(shadow);
            require(renderer.temporalFrameCount() == 0, "Shadow setting change retained history");
            frame();
            require(renderer.temporalFrameCount() == 1, "Shadow history did not restart");
            settings.model = math::translation({0.01f, 0, 0}) * settings.model;
            frame();
            require(renderer.temporalFrameCount() == 2, "Shadowed model motion reset TAA");
        }
        renderer.waitIdle();
        require(renderer.validationErrorCount() == 0 &&
                    diagnostics.str().find("Vulkan validation error") == std::string::npos,
                "Shadow path produced Vulkan validation errors");
        std::cout << "Shadow map: " << captures
                  << " analytic captures; occlusion, selected direct light, Mask/Blend, PCF, spot and AA/clip "
                     "conventions; Vulkan errors: 0\n";
    } catch (const std::exception& error) {
        std::cout << "Shadow-map test failed: " << error.what() << '\n';
        result = 1;
    }
    std::cerr.rdbuf(previous);
    if (result) std::cerr << diagnostics.str();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
