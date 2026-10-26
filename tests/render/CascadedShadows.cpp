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
constexpr unsigned extent = 321;
constexpr math::Vec3 base{0.4f, 0.4f, 0.4f};
constexpr float roughness = 0.8f;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct Image {
    std::vector<unsigned char> rgb;
    int red(std::array<int, 2> at) const { return rgb[(std::size_t(at[1]) * extent + at[0]) * 3]; }
};
Image read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string magic;
    unsigned width{}, height{}, maximum{};
    input >> magic >> width >> height >> maximum;
    require(magic == "P6" && width == extent && height == extent && maximum == 255 && input.get() == '\n',
            "Invalid cascade capture");
    Image image{std::vector<unsigned char>(extent * extent * 3)};
    input.read(reinterpret_cast<char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    require(input.gcount() == static_cast<std::streamsize>(image.rgb.size()), "Truncated cascade capture");
    return image;
}
int encoded(double linear) {
    require(std::isfinite(linear), "Cascade PBR reference is nonfinite");
    // The linear scene target is quantized to RGBA8 before capture's sRGB encoding.
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
    const std::array<int, 2> at{int(std::lround((ndc.x + 1) * extent * 0.5f - 0.5f)),
                                int(std::lround((ndc.y + 1) * extent * 0.5f - 0.5f))};
    require(at[0] >= 0 && at[1] >= 0 && at[0] < int(extent) && at[1] < int(extent), "Cascade probe outside viewport");
    return at;
}
math::Vec3 planePoint(const render::RenderSettings& settings, std::array<int, 2> at, math::Vec3 point,
                      math::Vec3 normal) {
    const auto inverse = math::inverse(canonical(settings));
    const float x = (float(at[0]) + 0.5f) * 2 / extent - 1, y = (float(at[1]) + 0.5f) * 2 / extent - 1;
    const auto a = math::transform_point(inverse, {x, y, 0});
    const auto b = math::transform_point(inverse, {x, y, 1});
    const auto ray = b - a;
    return a + ray * (math::dot(point - a, normal) / math::dot(ray, normal));
}
Color expected(const render::RenderSettings& settings, math::Vec3 point, math::Vec3 normal, double visibility,
               unsigned selected = 0) {
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
        for (unsigned c = 0; c < 3; ++c) result[c] += direct[c] * (i == selected ? visibility : 1);
    }
    return result;
}
void pixel(const Image& image, std::array<int, 2> at, Color want, const std::string& label, int tolerance = 5) {
    for (unsigned c = 0; c < 3; ++c) {
        const int actual = image.rgb[(std::size_t(at[1]) * extent + at[0]) * 3 + c];
        require(std::abs(actual - encoded(want[c])) <= tolerance, label + " channel " + std::to_string(c) +
                                                                      " expected " + std::to_string(encoded(want[c])) +
                                                                      ", got " + std::to_string(actual));
    }
}
void quad(assets::ModelData& model, std::array<math::Vec3, 4> points, math::Vec3 normal) {
    auto& mesh = model.meshes.emplace_back();
    for (unsigned i = 0; i < 4; ++i)
        mesh.vertices.push_back({points[i], normal, {float(i == 1 || i == 2), float(i >= 2)}});
    mesh.indices = {0, 1, 2, 0, 2, 3};
}
void flatQuad(assets::ModelData& model, math::Vec3 center, float halfX, float halfY, math::Vec3 normal = {0, 0, 1}) {
    quad(model,
         {{{center.x - halfX, center.y - halfY, center.z},
           {center.x + halfX, center.y - halfY, center.z},
           {center.x + halfX, center.y + halfY, center.z},
           {center.x - halfX, center.y + halfY, center.z}}},
         normal);
}
assets::ModelData emptyModel(const char* name) {
    assets::ModelData result;
    result.name = name;
    auto& material = result.materials.emplace_back();
    material.baseColor = math::Vec4(base, 1);
    material.roughnessFactor = roughness;
    return result;
}
struct Fixture {
    assets::ModelData model;
    std::vector<math::Vec3> dark, lit;
};
Fixture tiles(bool perspective, const std::vector<float>& depths = {3, 7, 11, 15}) {
    Fixture fixture{emptyModel("Four independent receiver/caster depth intervals"), {}, {}};
    const float tangent = std::tan(0.5f);
    for (unsigned i = 0; i < depths.size(); ++i) {
        const float d = depths[i], ndcX = -0.66f + 1.32f * float(i) / float(depths.size() - 1);
        const float scale = perspective ? d * tangent : 4;
        const math::Vec3 center{ndcX * scale, -0.25f * scale, -d};
        const float half = perspective ? scale * 0.14f : 0.65f;
        flatQuad(fixture.model, center, half, half);
        flatQuad(fixture.model, center + math::Vec3{0, 0.9f, 1.5f}, half * 0.45f, half * 0.4f);
        fixture.dark.push_back(center);
        fixture.lit.push_back(center + math::Vec3{half * 0.8f, 0, 0});
    }
    return fixture;
}
Fixture edge() {
    Fixture fixture{emptyModel("PCF boundary with constant analytic radiance"), {{0.3f, 0, -3}}, {{1.7f, 0, -3}}};
    flatQuad(fixture.model, {0, 0, -3}, 2, 2);
    flatQuad(fixture.model, {-0.6f, 0, -1.5f}, 0.7f, 1);
    return fixture;
}
Fixture ramp() {
    Fixture fixture{emptyModel("Continuous shadow across all cascade overlaps"), {}, {}};
    const auto normal = math::normalize(math::Vec3{2, 0, 1});
    quad(fixture.model, {{{-4, -1.8f, -1}, {4, -1.8f, -17}, {4, 1.8f, -17}, {-4, 1.8f, -1}}}, normal);
    // The lower edge projects to y=0 on the receiver. A coarse PCF map makes
    // different cascade texel grids observable along this continuous edge.
    quad(fixture.model, {{{-4, 0.9f, 0.5f}, {4, 0.9f, -15.5f}, {4, 3.4f, -15.5f}, {-4, 3.4f, 0.5f}}}, normal);
    return fixture;
}
}

int main(int argc, char** argv) {
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(360, 280, "AEG cascaded-shadow test", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    std::ostringstream diagnostics;
    auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
    int result{};
    unsigned captures{};
    try {
        const std::filesystem::path output(argv[1]);
        std::filesystem::create_directories(output);
        {
            tests::TestRenderer renderer(window, true);
            require(renderer.supportsShadows(), "GPU lacks sampled-depth cascade support");
            render::RenderSettings settings;
            settings.background = {0, 0, 0, 1};
            settings.ambient = 0.12f;
            settings.normalMapping = false;
            settings.lightCount = 2;
            settings.lights[0].direction = math::normalize(math::Vec3{0, -0.6f, -1});
            settings.lights[0].color = {1, 0, 0};
            settings.lights[0].intensity = 1;
            settings.lights[1].direction = {0, 0, -1};
            settings.lights[1].color = {0, 1, 0};
            settings.lights[1].intensity = 0.65f;
            const auto camera = [&](bool perspective, math::ClipSpaceConvention clip = {true, false},
                                    math::Mat4 cameraWorld = math::identity(), float far = 17) {
                settings.set_camera({math::inverse(cameraWorld),
                                     perspective ? math::perspective(1, 1, 1, far, clip)
                                                 : math::orthographic(-4, 4, -4, 4, 1, far, clip),
                                     clip});
            };
            camera(false);
            render::ShadowSettings shadow;
            shadow.enabled = true;
            shadow.cascadeCount = 4;
            shadow.splitLambda = 0;
            shadow.resolution = std::min(512u, renderer.maximumShadowResolution());
            renderer.setShadows(shadow);
            const auto frame = [&] {
                bool drew{};
                for (unsigned retry = 0; retry < 8 && !drew; ++retry) {
                    glfwPollEvents();
                    renderer.drawFrame(settings, [&] {
                        renderer.resizeViewport(extent, extent);
                        ImGui::Begin("Cascaded shadow analytic viewport");
                        ImGui::Image(renderer.viewportTexture(), {320, 320});
                        ImGui::End();
                        drew = true;
                    });
                }
                require(drew, "No cascaded-shadow frame submitted");
            };
            const auto capture = [&] {
                for (unsigned i = 0; i < (renderer.antialiasing().taa ? 8u : 2u); ++i) frame();
                const auto path = output / ("cascade-" + std::to_string(captures++) + ".ppm");
                renderer.captureViewport(path);
                return read(path);
            };
            const auto probe = [&](const Image& image, math::Vec3 localPoint, double visibility,
                                   const std::string& label, math::Vec3 localNormal = {0, 0, 1}) {
                const auto point = math::transform_point(settings.model, localPoint);
                const auto normal = math::normalize(math::transform_vector(settings.model, localNormal));
                const auto planeNormal = math::normalize(math::transform_vector(settings.model, {0, 0, 1}));
                const auto at = screen(settings, point);
                pixel(image, at, expected(settings, planePoint(settings, at, point, planeNormal), normal, visibility),
                      label);
            };
            auto fixture = tiles(false);
            renderer.uploadModel(fixture.model);
            // Capacity is four, but a first Spot frame renders one real image.
            // Vulkan validation also checks that its other bindings are initialized.
            settings.lights[0].type = render::LightType::Spot;
            settings.lights[0].position = {0, 0, 0};
            settings.lights[0].direction = {0, 0, -1};
            settings.lights[0].range = 30;
            settings.lights[0].intensity = 20;
            settings.lights[0].innerConeAngle = math::to_radians(35);
            settings.lights[0].outerConeAngle = math::to_radians(60);
            probe(capture(), fixture.lit[0], 1, "First Spot frame with four-map resource capacity");
            settings.lights[0].type = render::LightType::Directional;
            settings.lights[0].intensity = 1;
            settings.lights[0].direction = math::normalize(math::Vec3{0, -0.6f, -1});
            std::vector<render::AntialiasingSettings> modes{{}, {1, true, false}};
            if (renderer.supportedMsaaSamples() & 4u) modes.push_back({4, true, false});
            if (renderer.supportsTemporalAA()) modes.push_back({1, true, true});
            for (const auto aa : modes) {
                renderer.setAntialiasing(aa);
                for (const bool perspective : {false, true}) {
                    fixture = tiles(perspective);
                    renderer.uploadModel(fixture.model);
                    for (const bool down : {false, true})
                        for (const bool reversed : {false, true}) {
                            camera(perspective, {down, reversed});
                            const auto image = capture();
                            for (unsigned i = 0; i < 4; ++i) {
                                probe(image, fixture.dark[i], 0, "Actual cascade " + std::to_string(i) + " occlusion");
                                probe(image, fixture.lit[i], 1,
                                      "Cascade " + std::to_string(i) + " retains unoccluded light");
                            }
                        }
                }
            }
            renderer.setAntialiasing({});
            // World z and radial eye distance differ from camera-view depth.
            const auto cameraWorld =
                math::translation({7, -2, 4}) * math::rotation({0, 1, 0}, 0.47f) * math::rotation({0, 0, 1}, 0.2f);
            settings.model = cameraWorld;
            settings.lights[0].direction =
                math::transform_vector(cameraWorld, math::normalize(math::Vec3{0, -0.6f, -1}));
            settings.lights[1].direction = math::transform_vector(cameraWorld, {0, 0, -1});
            camera(true, {false, true}, cameraWorld);
            fixture = tiles(true);
            renderer.uploadModel(fixture.model);
            const auto rotated = capture();
            for (unsigned i = 0; i < 4; ++i)
                probe(rotated, fixture.dark[i], 0, "Rotated camera-view cascade selection");
            settings.model = math::identity();
            settings.lights[0].direction = math::normalize(math::Vec3{0.6f, 0, -1});
            settings.lights[1].direction = {0, 0, -1};
            camera(false);
            fixture = edge();
            renderer.uploadModel(fixture.model);
            shadow.resolution = 64;
            shadow.distanceFadeFraction = 0;
            std::array<unsigned, 3> intermediate{};
            for (unsigned radius = 0; radius <= 2; ++radius) {
                shadow.pcfRadius = radius;
                renderer.setShadows(shadow);
                const auto image = capture();
                probe(image, fixture.dark[0], 0, "PCF preserves fully blocked interior");
                probe(image, fixture.lit[0], 1, "PCF preserves fully lit exterior");
                const int dark = encoded(expected(settings, {0, 0, -3}, {0, 0, 1}, 0)[0]);
                const int bright = encoded(expected(settings, {0, 0, -3}, {0, 0, 1}, 1)[0]);
                const auto row = screen(settings, {0, 0, -3})[1];
                const auto left = screen(settings, {-0.8f, 0, -3})[0], right = screen(settings, {1.6f, 0, -3})[0];
                for (int x = left; x <= right; ++x) {
                    const int red = image.red({x, row});
                    if (red > dark + 4 && red < bright - 4) ++intermediate[radius];
                }
            }
            require(intermediate[0] == 0 && intermediate[1] > 0 && intermediate[2] > intermediate[1],
                    "PCF 1/9/25 comparisons do not produce distinct hard/3x3/5x5 edge widths");
            settings.lights[0].direction = math::normalize(math::Vec3{0, -0.6f, -1});
            shadow.resolution = std::min(512u, renderer.maximumShadowResolution());
            shadow.pcfRadius = 0;
            shadow.maxDistance = 16;
            shadow.distanceFadeFraction = 0.5f;
            renderer.setShadows(shadow);
            camera(false, {true, false}, math::identity(), 21);
            // Linear splits1..16 are4.75,8.5,12.25,16. Fade starts14.125.
            // The midpoint15.0625 has visibility0.5 for smooth or linear fade.
            fixture = tiles(false, {13, 15.0625f, 17.5f});
            renderer.uploadModel(fixture.model);
            const auto faded = capture();
            probe(faded, fixture.dark[0], 0, "Before last-cascade distance fade");
            probe(faded, fixture.dark[1], 0.5, "Last-cascade distance-fade midpoint");
            probe(faded, fixture.dark[2], 1, "Past configured shadow distance is lit");
            shadow.cascadeCount = 1;
            shadow.maxDistance = 4;
            renderer.setShadows(shadow);
            const auto legacy = capture();
            for (const auto point : fixture.dark)
                probe(legacy, point, 0, "Count-one legacy fit ignores camera cutoff and fade");
            shadow.cascadeCount = 4;
            shadow.maxDistance = 0;
            shadow.distanceFadeFraction = 0;
            shadow.resolution = 128;
            shadow.pcfRadius = 1;
            shadow.blendFraction = 0;
            renderer.setShadows(shadow);
            camera(true);
            fixture = ramp();
            renderer.uploadModel(fixture.model);
            const auto sharp = capture();
            shadow.blendFraction = 0.3f;
            renderer.setShadows(shadow);
            const auto blended = capture();
            const auto rampNormal = math::normalize(math::Vec3{2, 0, 1});
            unsigned changed{};
            int sharpJump{}, smoothJump{};
            for (const float split : {5.0f, 9.0f, 13.0f}) {
                const float x = (split - 9) / 2;
                const auto center = screen(settings, {x, 0, -split});
                for (int y = center[1] - 12; y <= center[1] + 12; ++y) {
                    const int a = sharp.red({center[0] - 1, y}), b = sharp.red({center[0] + 1, y});
                    const int c = blended.red({center[0] - 1, y}), d = blended.red({center[0] + 1, y});
                    sharpJump = std::max(sharpJump, std::abs(a - b));
                    smoothJump = std::max(smoothJump, std::abs(c - d));
                    changed += std::abs(a - c) > 4 || std::abs(b - d) > 4;
                }
                for (const float offset : {-0.15f, 0.0f, 0.15f}) {
                    const math::Vec3 dark{x + offset, 0.5f, -(9 + 2 * (x + offset))};
                    const auto at = screen(settings, dark);
                    const auto actualPoint = planePoint(settings, at, dark, rampNormal);
                    pixel(blended, at, expected(settings, actualPoint, rampNormal, 0),
                          "No bright seam in blended overlap");
                }
            }
            require(changed > 0, "Cascade overlap blend did not alter the observable coarse-map transition");
            require(sharpJump - smoothJump >= 4 && smoothJump * 4 <= sharpJump * 3,
                    "Cascade blend did not smooth the discontinuity at a split: hard=" + std::to_string(sharpJump) +
                        ", blended=" + std::to_string(smoothJump));
            // A single nearest 3x3 map can return only k/9, regardless of its
            // fit or texel phase. Cross-cascade mixing must produce radiance
            // between those ten independently predicted levels. Filter out
            // pixels whose unblended reference is within a quantization boundary
            // ambiguity, then require continuous levels in every overlap band.
            std::array<unsigned, 3> continuous{};
            constexpr std::array<float, 3> splits{5, 9, 13};
            for (int x = 1; x < int(extent) - 1; ++x)
                for (int y = int(extent / 2) - 18; y <= int(extent / 2) + 18; ++y) {
                    const auto point = planePoint(settings, {x, y}, {0, 0, -9}, rampNormal);
                    const float depth = math::dot(point - settings.cameraPosition, -settings.cameraTowardViewer);
                    const auto band = std::ranges::find_if(
                        splits, [&](float split) { return depth > split - 1.2f + 0.05f && depth < split - 0.05f; });
                    if (band == splits.end()) continue;
                    int hardDistance = 255, blendedDistance = 255;
                    for (unsigned tap = 0; tap <= 9; ++tap) {
                        const int level = encoded(expected(settings, point, rampNormal, double(tap) / 9)[0]);
                        hardDistance = std::min(hardDistance, std::abs(sharp.red({x, y}) - level));
                        blendedDistance = std::min(blendedDistance, std::abs(blended.red({x, y}) - level));
                    }
                    if (hardDistance <= 1 && blendedDistance >= 2) ++continuous[std::size_t(band - splits.begin())];
                }
            require(
                std::ranges::all_of(continuous, [](unsigned count) { return count >= 4; }) &&
                    continuous[0] + continuous[1] + continuous[2] >= 16,
                "Cascade overlap lacks visibility levels beyond a single 3x3 map: " + std::to_string(continuous[0]) +
                    "/" + std::to_string(continuous[1]) + "/" + std::to_string(continuous[2]));
            std::cout << "Cascade overlap continuous-level probes: " << continuous[0] << '/' << continuous[1] << '/'
                      << continuous[2] << "; split red discontinuity " << sharpJump << " -> " << smoothJump << '\n';
            // Off-camera caster shares light XY with the receiver but lies far
            // beyond every camera slice in light Z. Slice-only caster Z loses it.
            auto adversary = emptyModel("Off-camera caster requires all-model light-Z bounds");
            const auto tilted = math::normalize(math::Vec3{-1, 0, 0.1f});
            flatQuad(adversary, {0, 0, -4}, 1, 1, tilted);
            flatQuad(adversary, {-100, 0, -14}, 4, 1.5f, tilted);
            renderer.uploadModel(adversary);
            settings.lights[0].direction = math::normalize(math::Vec3{10, 0, 1});
            settings.set_camera(
                {math::identity(), math::orthographic(-2, 2, -2, 2, 0.1f, 10, {true, false}), {true, false}});
            shadow.resolution = std::min(512u, renderer.maximumShadowResolution());
            shadow.pcfRadius = 0;
            renderer.setShadows(shadow);
            probe(capture(), {0, 0, -4}, 0, "Off-camera caster survives light-Z fitting", tilted);
            renderer.waitIdle();
            require(renderer.validationErrorCount() == 0, "Cascade path produced Vulkan validation errors");
        }
        // Keep stderr capture active through destruction: descriptor/image
        // lifetime validation failures often arrive during teardown.
        require(diagnostics.str().find("Vulkan validation error") == std::string::npos,
                "Cascade setup, rendering or teardown produced Vulkan validation errors");
        std::cout << "Cascaded shadows: " << captures
                  << " analytic captures; four cascade intervals, selected direct light, all AA/clip conventions, "
                     "camera depth, PCF, blend, fade and off-camera casters; Vulkan errors: 0\n";
    } catch (const std::exception& error) {
        std::cout << "Cascaded-shadow test failed: " << error.what() << '\n';
        result = 1;
    }
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cerr.rdbuf(previous);
    if (result) std::cerr << diagnostics.str();
    return result;
}
