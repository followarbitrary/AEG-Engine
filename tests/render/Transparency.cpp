#include "RendererHarness.hpp"
#include <ModelImporter.hpp>
#include <OrbitCamera.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace aeg;
using Color = std::array<double, 3>;
constexpr Color blue{0, 0, 1}, red{1, 0, 0}, white{1, 1, 1};
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct Image {
    unsigned width{}, height{};
    std::vector<unsigned char> rgb;
};
Image read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Image result;
    std::string magic;
    unsigned maximum{};
    input >> magic >> result.width >> result.height >> maximum;
    require(magic == "P6" && maximum == 255 && result.width && result.height && input.get() == '\n',
            "Invalid transparency capture header");
    result.rgb.resize(std::size_t(result.width) * result.height * 3);
    input.read(reinterpret_cast<char*>(result.rgb.data()), std::streamsize(result.rgb.size()));
    require(input.gcount() == std::streamsize(result.rgb.size()) && input.peek() == std::char_traits<char>::eof(),
            "Truncated or oversized transparency capture");
    return result;
}
Color over(Color foreground, double alpha, Color background) {
    for (unsigned c = 0; c < 3; ++c) foreground[c] = foreground[c] * alpha + background[c] * (1 - alpha);
    return foreground;
}
double linear(unsigned value) {
    const double s = double(value) / 255;
    return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}
int encoded(double value) {
    value = std::clamp(value, 0.0, 1.0);
    const auto srgb = value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - 0.055;
    return int(std::lround(srgb * 255));
}
void patch(const Image& image, Color expected, const std::string& label, double u = 0.5, double v = 0.5) {
    const int x = int(u * image.width), y = int(v * image.height);
    const int radius = image.width >= 5 && image.height >= 5 ? 2 : 0;
    require(x >= radius && y >= radius && x + radius < int(image.width) && y + radius < int(image.height),
            "Transparency probe is outside its capture");
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            for (unsigned c = 0; c < 3; ++c) {
                const int actual = image.rgb[(std::size_t(y + dy) * image.width + x + dx) * 3 + c];
                require(std::abs(actual - encoded(expected[c])) <= 3,
                        label + ": channel " + std::to_string(c) + " expected " + std::to_string(encoded(expected[c])) +
                            ", got " + std::to_string(actual));
            }
}
double interiorError(const Image& actual, const Image& expected) {
    require(actual.width == expected.width && actual.height == expected.height, "Transparency extent changed");
    double error{};
    std::size_t count{};
    for (unsigned y = actual.height / 3; y < actual.height * 2 / 3; ++y)
        for (unsigned x = actual.width / 3; x < actual.width * 2 / 3; ++x)
            for (unsigned c = 0; c < 3; ++c) {
                const auto i = (std::size_t(y) * actual.width + x) * 3 + c;
                error += std::abs(int(actual.rgb[i]) - int(expected.rgb[i]));
                ++count;
            }
    require(count > 1000, "Transparency comparison has too few interior samples");
    return error / count;
}
assets::MeshData quad(float halfSize, float z, unsigned material) {
    assets::MeshData result;
    result.materialIndex = material;
    result.vertices = {{{-halfSize, -halfSize, z}, {0, 0, 1}, {0, 0}},
                       {{halfSize, -halfSize, z}, {0, 0, 1}, {1, 0}},
                       {{halfSize, halfSize, z}, {0, 0, 1}, {1, 1}},
                       {{-halfSize, halfSize, z}, {0, 0, 1}, {0, 1}}};
    result.indices = {0, 1, 2, 0, 2, 3};
    return result;
}
assets::MaterialData material(Color rgb, float alpha, assets::AlphaMode mode = assets::AlphaMode::Blend) {
    assets::MaterialData result;
    result.baseColor = {float(rgb[0]), float(rgb[1]), float(rgb[2]), alpha};
    result.alphaMode = mode;
    return result;
}
assets::ModelData single(assets::MaterialData value, float halfSize = 1) {
    assets::ModelData result;
    result.materials.push_back(std::move(value));
    result.meshes.push_back(quad(halfSize, -3, 0));
    return result;
}
assets::ModelData layers(bool reverse = false, bool opaqueMiddle = false, bool opaqueFront = false) {
    assets::ModelData result;
    result.materials = {material(red, 0.25f), material({0, 1, 0}, 0.5f)};
    // Deliberately begin with near geometry; preserving source order must fail the color oracle.
    result.meshes = {quad(1, -2, 1), quad(1, -4, 0)};
    if (opaqueMiddle || opaqueFront) {
        result.materials.push_back(material(opaqueFront ? white : blue, 0, assets::AlphaMode::Opaque));
        result.meshes.push_back(quad(3, opaqueFront ? -1.5f : -3.0f, 2));
    }
    if (reverse) std::reverse(result.meshes.begin(), result.meshes.end());
    return result;
}
assets::ImageData wave(bool foreground) {
    assets::ImageData result{256, 1, {}};
    for (unsigned x = 0; x < 256; ++x) {
        const auto phase = (x + (foreground ? 19u : 0u)) % (foreground ? 128u : 64u);
        const auto half = foreground ? 64u : 32u;
        const auto value = static_cast<unsigned char>(48 + 176 * std::min(phase, 2 * half - phase) / half);
        result.rgba.insert(result.rgba.end(),
                           {value, value, value, static_cast<unsigned char>(foreground ? 128 : 255)});
    }
    return result;
}
assets::ModelData textured(bool transparent, bool alphaZero = false) {
    assets::ModelData result;
    auto rear = material(white, 1, assets::AlphaMode::Opaque);
    rear.texture = wave(false);
    rear.hasTexture = true;
    result.materials.push_back(std::move(rear));
    result.meshes.push_back(quad(3, -4, 0));
    if (transparent || alphaZero) {
        auto front = material(white, alphaZero ? 0.0f : 1.0f);
        front.texture = wave(true);
        front.hasTexture = true;
        result.materials.push_back(std::move(front));
        result.meshes.push_back(quad(alphaZero ? 20.0f : 1.25f, -2, 1));
    }
    return result;
}
std::array<double, 4> centerTexel(const assets::ImageData& image) {
    std::array<double, 4> sample{};
    for (const unsigned y : {(image.height - 1) / 2, image.height / 2})
        for (const unsigned x : {(image.width - 1) / 2, image.width / 2})
            for (unsigned c = 0; c < 4; ++c) {
                const auto value = image.rgba[(std::size_t(y) * image.width + x) * 4 + c];
                sample[c] += (c == 3 ? double(value) / 255 : linear(value)) * 0.25;
            }
    return sample;
}
void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), std::streamsize(text.size()));
    require(bool(file), "Cannot write original transparency fixture");
}
void appendU32(std::vector<unsigned char>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<unsigned char>(value >> shift));
}
void writeOriginalGlb(const std::filesystem::path& path) {
    std::string json =
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":44}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,-1,-3],"max":[1,1,-3]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"materials":[{"alphaMode":"BLEND","pbrMetallicRoughness":{"baseColorFactor":[0.2,0.6,0.8,0.35]}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    while (json.size() % 4) json.push_back(' ');
    std::vector<unsigned char> bytes;
    appendU32(bytes, 0x46546C67);
    appendU32(bytes, 2);
    appendU32(bytes, std::uint32_t(12 + 8 + json.size() + 8 + 44));
    appendU32(bytes, std::uint32_t(json.size()));
    appendU32(bytes, 0x4E4F534A);
    bytes.insert(bytes.end(), json.begin(), json.end());
    appendU32(bytes, 44);
    appendU32(bytes, 0x004E4942);
    // Serialize explicit scalar components, never the padded public Vec3 type.
    const std::array<float, 9> positions{-1, -1, -3, 1, -1, -3, 1, 1, -3};
    const auto start = bytes.size();
    bytes.resize(start + 44);
    std::memcpy(bytes.data() + start, positions.data(), sizeof(positions));
    bytes[start + 38] = 1;
    bytes[start + 40] = 2;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    require(bool(file), "Cannot write original GLB transparency fixture");
}
} // namespace

int main(int argc, char** argv) {
    if ((argc != 2 && argc != 3) || (argc == 3 && std::string_view(argv[2]) != "--pmx-smoke") || !glfwInit()) return 1;
    const bool pmxSmoke = argc == 3;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(400, 320, "AEG transparency regression", nullptr, nullptr);
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
        settings.unlit = true;
        settings.background = {0, 0, 1, 1};
        unsigned width = 128, height = 128, captures{};
        auto frame = [&] {
            bool drew{};
            for (unsigned retry = 0; retry < 8 && !drew; ++retry) {
                glfwPollEvents();
                renderer.drawFrame(settings, [&] {
                    renderer.resizeViewport(width, height);
                    ImGui::Begin("Transparency");
                    ImGui::Image(renderer.viewportTexture(), {float(width), float(height)});
                    ImGui::End();
                    drew = true;
                });
            }
            require(drew, "No transparency frame submitted");
        };
        auto capture = [&] {
            const auto path = output / ("transparency-" + std::to_string(captures++) + ".ppm");
            renderer.captureViewport(path);
            auto image = read(path);
            require(image.width == width && image.height == height, "Transparency capture retained a stale viewport");
            return image;
        };
        auto show = [&](const assets::ModelData& model, render::AntialiasingSettings aa, unsigned frames = 0) {
            renderer.setAntialiasing(aa);
            renderer.uploadModel(model);
            for (unsigned i = 0; i < (frames ? frames : aa.taa ? 8u : 2u); ++i) frame();
            return capture();
        };
        if (pmxSmoke) {
            const auto model = assets::loadModel(std::filesystem::path(AEG_TEST_FIXTURES) / "pmx-alpha.pmx");
            require(model.meshes.size() == 2, "PMX alpha fixture lost its authored material groups");
            const auto& partial = model.materials.at(model.meshes[0].materialIndex);
            const auto& hidden = model.materials.at(model.meshes[1].materialIndex);
            require(partial.alphaMode == assets::AlphaMode::Blend && std::abs(partial.baseColor.w - 0.4f) < 1e-5f &&
                        hidden.alphaMode == assets::AlphaMode::Blend && hidden.baseColor.w == 0,
                    "PMX smoke did not import the actual partial and hidden materials");
            width = 698;
            height = 740;
            settings.background = {1, 1, 1, 1};
            camera::OrbitCamera fitted;
            fitted.fit(model.bounds);
            settings.set_camera(fitted.render_camera(float(width) / height, {true, false}));
            const auto image = show(model, {});
            require(image.rgb[0] == 255 && image.rgb[1] == 255 && image.rgb[2] == 255,
                    "Controlled PMX smoke background was not white");
            std::size_t foreground{};
            std::set<std::uint32_t> colors;
            for (std::size_t i = 0; i < image.rgb.size(); i += 3) {
                const int difference = 255 - image.rgb[i] + 255 - image.rgb[i + 1] + 255 - image.rgb[i + 2];
                if (difference > 45) ++foreground;
                colors.insert((std::uint32_t(image.rgb[i]) << 16) | (std::uint32_t(image.rgb[i + 1]) << 8) |
                              image.rgb[i + 2]);
            }
            // Retain the original smoke check's 1% coverage, L1>45 contrast and
            // eight-color requirements. An unlit model and white background keep
            // genuine alpha=.4 geometry measurable without loosening the oracle.
            require(foreground >= std::size_t(width) * height / 100 && colors.size() >= 8,
                    "Actual translucent PMX geometry is absent or not visibly rendered");
            std::cout << "PMX alpha smoke: " << foreground << " foreground pixels, " << colors.size() << " colors\n";
        } else {
            const auto camera = [&](bool perspective, math::ClipSpaceConvention convention,
                                    math::Mat4 view = math::identity()) {
                const auto projection = perspective
                                            ? math::perspective(math::to_radians(60), 1, 0.1f, 20, convention)
                                            : math::orthographic(-1.5f, 1.5f, -1.5f, 1.5f, 0.1f, 20, convention);
                settings.set_camera({view, projection, convention});
            };
            std::vector<render::AntialiasingSettings> modes{{}, {1, true, false}};
            if (renderer.supportedMsaaSamples() & 4u) {
                modes.push_back({4, false, false});
                modes.push_back({4, true, false});
            }
            if (renderer.supportsTemporalAA()) {
                modes.push_back({1, false, true});
                modes.push_back({1, true, true});
            }
            const auto normalColor = over({0, 1, 0}, 0.5, over(red, 0.25, blue));
            const auto reversedColor = over(red, 0.25, over({0, 1, 0}, 0.5, blue));
            for (const auto aa : modes) {
                camera(false, {true, false});
                settings.model = math::identity();
                patch(show(single(material(red, 0.25f)), aa), over(red, 0.25, blue), "Single alpha layer");
                patch(show(layers(), aa), normalColor, "Two sorted alpha layers");
                patch(show(layers(true), aa), normalColor, "Opposite input order");
                patch(show(layers(false, true), aa), over({0, 1, 0}, 0.5, blue),
                      "Opaque geometry between alpha layers");
                patch(show(layers(true, false, true), aa), white, "Opaque foreground must occlude alpha geometry");
                patch(show(single(material(red, 0)), aa), blue, "Zero alpha must leave color and depth untouched");
                patch(show(single(material(red, 1)), aa), red, "Alpha one");
                patch(show(single(material(red, 0, assets::AlphaMode::Opaque)), aa), red,
                      "Opaque ignores factor alpha");
                auto mask = material(red, 0.3f, assets::AlphaMode::Mask);
                patch(show(single(mask), aa), blue, "Mask default cutoff discards");
                mask.alphaCutoff = 0.1f;
                patch(show(single(mask), aa), red, "Surviving mask is opaque");
                auto tinted = material({0.8, 0.5, 0.25}, 0.4f);
                tinted.hasTexture = true;
                tinted.texture = {1, 1, {204, 153, 230, 64}};
                Color textureColor{0.8 * linear(204), 0.5 * linear(153), 0.25 * linear(230)};
                patch(show(single(tinted), aa), over(textureColor, 0.4 * 64 / 255, blue),
                      "Texture alpha times factor, with linear RGB blending");
                tinted.alphaMode = assets::AlphaMode::Opaque;
                tinted.baseColor.w = 0;
                tinted.texture.rgba[3] = 0;
                patch(show(single(tinted), aa), textureColor, "Opaque ignores texture alpha too");
            }
            // Calibrate the existing two-sided normal policy, then verify that
            // Lambert lighting and the normal map are applied before alpha blending.
            camera(false, {true, false});
            settings.unlit = false;
            settings.ambient = 0.2f;
            settings.normalMapping = false;
            auto litMaterial = material(red, 0.5f);
            litMaterial.hasNormalMap = true;
            litMaterial.normalTexture = {1, 1, {255, 128, 128, 255}};
            auto litModel = single(litMaterial);
            for (auto& vertex : litModel.meshes[0].vertices) vertex.tangent = {1, 0, 0, 1};
            settings.lightDirection = {0, 0, -1};
            const auto positive = show(litModel, {});
            settings.lightDirection = {0, 0, 1};
            const auto negative = show(litModel, {});
            const auto center = (std::size_t(height / 2) * width + width / 2) * 3;
            require(std::abs(int(positive.rgb[center]) - int(negative.rgb[center])) > 40,
                    "Blended material did not respond to geometric lighting");
            const float faceSign = positive.rgb[center] > negative.rgb[center] ? 1.0f : -1.0f;
            patch(faceSign > 0 ? positive : negative, over(red, 0.5, blue), "Lit blended geometric normal");
            patch(faceSign > 0 ? negative : positive, over({0.2, 0, 0}, 0.5, blue), "Blended ambient light");
            settings.normalMapping = true;
            settings.lightDirection = {-faceSign, 0, 0};
            const double encodedZero = 1.0 / 255;
            const double mappedLight = 0.2 + 0.8 / std::sqrt(1 + 2 * encodedZero * encodedZero);
            std::vector<render::AntialiasingSettings> litModes{{}};
            if (renderer.supportsTemporalAA()) litModes.push_back({1, true, true});
            for (const auto aa : litModes) {
                settings.normalStrength = 1;
                patch(show(litModel, aa), over({mappedLight, 0, 0}, 0.5, blue), "Normal-mapped blended lighting");
                settings.normalStrength = 0;
                patch(show(litModel, aa), over({0.2, 0, 0}, 0.5, blue),
                      "Zero normal strength preserves blended opacity");
            }
            settings.unlit = true;
            settings.ambient = 0.28f;
            settings.normalStrength = 1;
            // View-space sorting must update for both projection kinds, every convention,
            // an opposite camera, and a model transform which exchanges the surfaces.
            for (const bool perspective : {false, true})
                for (const bool yDown : {false, true})
                    for (const bool reversed : {false, true}) {
                        const math::ClipSpaceConvention convention{yDown, reversed};
                        const auto label = std::string(perspective ? "Perspective" : "Ortho") +
                                           (yDown ? " Y-down" : " Y-up") + (reversed ? " reversed-Z" : " normal-Z");
                        settings.model = math::identity();
                        camera(perspective, convention);
                        patch(show(layers(), {}), normalColor, label + " normal sorting");
                        patch(show(layers(true), {}), normalColor, label + " opposite mesh order");
                        camera(perspective, convention, math::look_at({0, 0, -6}, {0, 0, -3}, {0, 1, 0}));
                        patch(show(layers(), {}), reversedColor, label + " opposite camera");
                        camera(perspective, convention);
                        settings.model = math::translation({0, 0, -6}) * math::rotation({0, 1, 0}, math::kPi);
                        patch(show(layers(), {}), reversedColor, label + " rotated model sorting");
                        settings.model = math::identity();
                        if (renderer.supportsTemporalAA())
                            patch(show(layers(), {1, false, true}), normalColor, label + " temporal blending");
                    }
            camera(false, {true, false});
            for (const auto aa : modes)
                for (const auto size : {std::array{135u, 97u}, std::array{1u, 1u}}) {
                    width = size[0];
                    height = size[1];
                    patch(show(single(material(red, 0.25f), 20), aa), over(red, 0.25, blue),
                          "Odd/single-pixel viewport");
                }
            width = height = 128;
            camera(false, {true, false});
            const auto glb = output / "original-blend.glb", obj = output / "original-blend.obj";
            writeOriginalGlb(glb);
            writeText(output / "original-blend.mtl", "newmtl alpha\nKd 0.2 0.6 0.8\nd 0.35\n");
            writeText(obj, "mtllib original-blend.mtl\nv -1 -1 -3\nv 1 -1 -3\nv 1 1 -3\nusemtl alpha\nf 1 2 3\n");
            const std::array importedPaths{std::filesystem::path(AEG_TEST_FIXTURES) / "alpha-blend.gltf", glb, obj,
                                           std::filesystem::path(AEG_TEST_FIXTURES) / "pmx-alpha.pmx"};
            const std::array expectedAlpha{0.3f, 0.35f, 0.35f, 0.4f};
            for (std::size_t i = 0; i < importedPaths.size(); ++i) {
                const auto imported = assets::loadModel(importedPaths[i]);
                const auto value = imported.materials.at(imported.meshes.at(0).materialIndex);
                require(value.alphaMode == assets::AlphaMode::Blend &&
                            std::abs(value.baseColor.w - expectedAlpha[i]) < 1e-5f,
                        "Imported material did not retain its blend mode and single opacity factor");
                auto model = single(value);
                for (auto& vertex : model.meshes[0].vertices) vertex.uv = {0.5f, 0.5f};
                const auto texel = centerTexel(value.texture);
                const Color color{value.baseColor.x * texel[0], value.baseColor.y * texel[1],
                                  value.baseColor.z * texel[2]};
                patch(show(model, {}), over(color, expectedAlpha[i] * texel[3], blue),
                      "Imported " + importedPaths[i].extension().string() + " alpha material");
            }
            if (renderer.supportsTemporalAA()) {
                camera(true, {true, false});
                settings.model = math::identity();
                const auto baseline = show(textured(false), {1, false, true}, 32);
                const auto invisible = show(textured(false, true), {1, false, true}, 32);
                require(interiorError(invisible, baseline) <= 0.25, "Zero-alpha material disturbed opaque TAA history");
                show(textured(true), {1, false, true}, 32);
                camera(true, {true, false}, math::translation({-0.13f, 0, 0}));
                // After 32 submitted frames, phase zero has no horizontal jitter.
                // The 1D textures make its vertical jitter irrelevant inside this patch.
                frame();
                const auto moving = capture();
                renderer.setAntialiasing({});
                frame();
                const auto current = capture();
                const auto error = interiorError(moving, current);
                require(error <= 0.35,
                        "Transparent TAA retained mixed-depth history after parallax: " + std::to_string(error));
                std::cout << "Transparency reactive parallax error: " << error << '\n';
            }
        }
        require(renderer.validationErrorCount() == 0, "Vulkan validation failed for transparency");
        renderer.waitIdle();
        if (pmxSmoke)
            std::cout << "PASS actual translucent PMX geometry, original visibility thresholds and validation\n";
        else
            std::cout << "PASS " << captures
                      << " transparency captures: analytical blend, ordering, depth, alpha modes, "
                         "texture/factor, imports, clip conventions, AA and reactive history\n";
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
