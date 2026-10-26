#include "RendererHarness.hpp"
#include <ModelImporter.hpp>
#include <MaterialTextures.hpp>
#include <Utf8Path.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <math.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
struct Image {
    std::uint32_t width{}, height{};
    std::vector<unsigned char> rgb;
};
Image readPpm(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Image image;
    std::string magic;
    unsigned maximum{};
    input >> magic >> image.width >> image.height >> maximum;
    require(magic == "P6" && maximum == 255 && image.width && image.height && input.get() == '\n',
            "invalid normal-map capture header: " + path.string());
    image.rgb.resize(static_cast<std::size_t>(image.width) * image.height * 3);
    input.read(reinterpret_cast<char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    require(input.gcount() == static_cast<std::streamsize>(image.rgb.size()) &&
                input.peek() == std::char_traits<char>::eof(),
            "incorrect normal-map capture size");
    return image;
}
void sameExtent(const Image& actual, const Image& expected) {
    require(actual.width == expected.width && actual.height == expected.height, "capture extent changed");
}
void samePixels(const Image& actual, const Image& expected, const std::string& label) {
    sameExtent(actual, expected);
    require(actual.rgb == expected.rgb, label + " changed the original vertex-normal pixels");
}
void nearPixels(const Image& actual, const Image& expected, int tolerance, const std::string& label) {
    sameExtent(actual, expected);
    for (std::size_t i = 0; i < actual.rgb.size(); ++i)
        require(std::abs(int(actual.rgb[i]) - int(expected.rgb[i])) <= tolerance,
                label + " exceeded " + std::to_string(tolerance) + " capture intensity levels");
}
std::size_t changedPixels(const Image& actual, const Image& expected, int threshold = 4) {
    sameExtent(actual, expected);
    std::size_t changed{};
    for (std::size_t i = 0; i < actual.rgb.size(); i += 3) {
        bool different{};
        for (std::size_t channel = 0; channel < 3; ++channel)
            different |= std::abs(int(actual.rgb[i + channel]) - int(expected.rgb[i + channel])) >= threshold;
        if (different) ++changed;
    }
    return changed;
}
void unchangedMaterial(const Image& actual, const Image& expected, const Image& mask, const std::string& label) {
    sameExtent(actual, expected);
    sameExtent(actual, mask);
    std::size_t checked{};
    for (std::size_t i = 0; i < mask.rgb.size(); i += 3) {
        // The mask was rendered with all geometry present: hidden surfaces of
        // this material cannot accidentally select the newly mapped material.
        if (mask.rgb[i] < 250) continue;
        for (std::size_t channel = 0; channel < 3; ++channel)
            require(actual.rgb[i + channel] == expected.rgb[i + channel], label + " changed another material");
        ++checked;
    }
    require(checked > 100, label + " did not expose enough of the other material");
}
void grayscale(const Image& image) {
    for (std::size_t i = 0; i < image.rgb.size(); i += 3)
        require(image.rgb[i] == image.rgb[i + 1] && image.rgb[i] == image.rgb[i + 2],
                "white normal-map fixture acquired a color tint");
}
double linear(unsigned char value) {
    const double srgb = double(value) / 255;
    return srgb <= 0.04045 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
}
// Exclude primitive/AA edges and decode the sRGB PPM before checking Lambert light.
double interior(const Image& image, float horizontalCenter = 0.5f) {
    require(image.width >= 30 && image.height >= 30, "interior fixture is too small");
    double sum{};
    std::size_t count{};
    unsigned low = 255, high{};
    for (unsigned y = image.height * 2 / 5; y < image.height * 3 / 5; ++y)
        for (unsigned x = unsigned(float(image.width) * (horizontalCenter - 0.08f));
             x < unsigned(float(image.width) * (horizontalCenter + 0.08f)); ++x) {
            const auto at = (static_cast<std::size_t>(y) * image.width + x) * 3;
            const auto value = image.rgb[at];
            require(value == image.rgb[at + 1] && value == image.rgb[at + 2], "interior is not grayscale");
            low = std::min(low, unsigned(value));
            high = std::max(high, unsigned(value));
            sum += linear(value);
            ++count;
        }
    require(count > 20 && high - low <= 2, "flat normal-map interior is missing or spatially corrupt");
    return sum / double(count);
}
constexpr float ambient = 0.12f;
void expectedLight(const Image& image, aeg::math::Vec3 normal, aeg::math::Vec3 towardLight, const std::string& label,
                   float horizontalCenter = 0.5f) {
    const double expected = ambient + (1 - ambient) * std::max(aeg::math::dot(normal, towardLight), 0.0f);
    const double actual = interior(image, horizontalCenter);
    require(std::abs(actual - expected) < 0.013, label + " has incorrect linear lighting: expected " +
                                                     std::to_string(expected) + ", got " + std::to_string(actual));
}
double patchLight(const Image& image, aeg::math::Vec2 center) {
    const int x = int(center.x * float(image.width));
    const int y = int(center.y * float(image.height));
    require(x > 1 && y > 1 && x + 1 < int(image.width) && y + 1 < int(image.height),
            "normal texture patch lies outside capture");
    double sum{};
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            sum += linear(image.rgb[(static_cast<std::size_t>(y + dy) * image.width + x + dx) * 3]);
    return sum / 9;
}
using Sample = std::array<unsigned char, 3>;
constexpr Sample neutral{128, 128, 255}, tiltedX{204, 128, 230}, tiltedY{128, 204, 230};
struct AutomaticPmxFiles {
    std::filesystem::path root;
    const std::filesystem::path baseName{u8"\u5f69\u8272 pad%20.png"};
    const std::filesystem::path normalName{u8"\u5f69\u8272 pad%20N.png"};

    AutomaticPmxFiles() {
        std::random_device random;
        bool created{};
        for (int attempt = 0; attempt < 16 && !created; ++attempt) {
            root =
                std::filesystem::temp_directory_path() /
                ("aeg-auto-normal-gpu-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                 "-" + std::to_string(random()));
            created = std::filesystem::create_directory(root);
        }
        require(created, "automatic PMX GPU fixture could not claim its own temporary directory");
    }
    ~AutomaticPmxFiles() {
        // Only remove this test's known files and then its empty directories.
        std::error_code error;
        std::filesystem::remove(root / "pmx-textures" / normalName, error);
        std::filesystem::remove(root / "pmx-textures" / baseName, error);
        std::filesystem::remove(root / "automatic.pmx", error);
        std::filesystem::remove(root / "pmx-textures", error);
        std::filesystem::remove(root, error);
    }
};
struct EffectPmxFiles : AutomaticPmxFiles {
    ~EffectPmxFiles() {
        // Remove only this fixture's additional files, then let the base
        // lifetime remove its known PMX/base files and empty root.
        std::error_code error;
        std::filesystem::remove(root / "effects" / "foreign-normal.png", error);
        std::filesystem::remove(root / "effects" / "Surface.fx", error);
        std::filesystem::remove(root / "automatic.emd", error);
        std::filesystem::remove(root / "Solid_n.png", error);
        std::filesystem::remove(root / "effects", error);
    }
};
aeg::assets::ModelData plane(Sample sample = tiltedX, aeg::math::Vec3 normal = {0, 0, 1}, aeg::math::Vec4 tangent = {1, 0, 0, 1},
                             float scale = 1) {
    aeg::assets::ModelData model;
    model.name = "Known tangent-frame normal-map fixture";
    aeg::assets::MeshData mesh;
    // Old three-field aggregate initialization remains valid; new fixtures
    // explicitly opt into a known tangent frame after creating their vertices.
    mesh.vertices = {{{-0.45f, -0.8f, 0.5f}, normal, {0, 0}},
                     {{0.45f, -0.8f, 0.5f}, normal, {1, 0}},
                     {{0.45f, 0.8f, 0.5f}, normal, {1, 1}},
                     {{-0.45f, 0.8f, 0.5f}, normal, {0, 1}}};
    for (auto& vertex : mesh.vertices) vertex.tangent = tangent;
    mesh.indices = {0, 1, 2, 0, 2, 3};
    model.meshes.push_back(std::move(mesh));
    auto& material = model.materials.emplace_back();
    material.hasNormalMap = true;
    material.normalTexture = {1, 1, {sample[0], sample[1], sample[2], 255}};
    material.normalScale = scale;
    return model;
}
aeg::assets::ModelData withoutMap(aeg::assets::ModelData model) {
    for (auto& material : model.materials) material.hasNormalMap = false;
    return model;
}
// Only the map contract: UNORM decode, scale XY, normalize. World frames below
// are hand-derived literals, independent of renderer transformation helpers.
aeg::math::Vec3 mapped(Sample sample, float scale, aeg::math::Vec3 n, aeg::math::Vec3 t, aeg::math::Vec3 b) {
    const aeg::math::Vec3 encoded(float(sample[0]) * 2 / 255 - 1, float(sample[1]) * 2 / 255 - 1,
                            float(sample[2]) * 2 / 255 - 1);
    return aeg::math::normalize(t * (encoded.x * scale) + b * (encoded.y * scale) + n * encoded.z);
}
}

int main(int argc, char** argv) {
    if (argc != 2 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(800, 600, "AEG normal mapping test", nullptr, nullptr);
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
        require(settings.normalMapping && settings.normalStrength == 1, "normal mapping must default enabled at 1");
        settings.background = {0, 0, 0, 1};
        settings.ambient = ambient;
        std::uint32_t width = 321, height = 239;
        auto frame = [&] {
            bool drew{};
            for (int retry = 0; retry < 8 && !drew; ++retry) {
                glfwPollEvents();
                renderer.drawFrame(settings, [&] {
                    renderer.resizeViewport(width, height);
                    ImGui::SetNextWindowPos({20, 30}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize({510, 410}, ImGuiCond_Always);
                    ImGui::Begin("Normal-map scaled viewport");
                    ImGui::Image(renderer.viewportTexture(), {457, 337});
                    ImGui::End();
                    drew = true;
                });
            }
            require(drew, "no normal-map frame submitted after resize");
        };
        auto advance = [&](unsigned count) {
            for (unsigned i = 0; i < count; ++i) frame();
        };
        auto capture = [&](const std::string& name, bool expectGrayscale = true) {
            const auto path = output / (name + ".ppm");
            renderer.captureViewport(path);
            auto image = readPpm(path);
            require(image.width == width && image.height == height, "capture has stale normal-map dimensions");
            if (expectGrayscale) grayscale(image);
            return image;
        };
        auto light = [&](aeg::math::Vec3 towardLight, const std::string& name) {
            settings.lightDirection = -towardLight;
            frame();
            return capture(name);
        };
        // Calibrate the existing back-face policy using geometric +/-Z light.
        auto faceSign = [&](const aeg::assets::ModelData& source, const std::string& label) {
            renderer.uploadModel(withoutMap(source));
            const auto positive = interior(light({0, 0, 1}, label + "-geometric-positive-z"));
            const auto negative = interior(light({0, 0, -1}, label + "-geometric-negative-z"));
            require(std::abs(positive - negative) > 0.3, label + " has no geometric-normal calibration");
            return positive > negative ? 1.0f : -1.0f;
        };
        constexpr std::array<aeg::math::Vec3, 6> axes{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
        auto response = [&](const aeg::assets::ModelData& source, aeg::math::Vec3 expectedNormal, const std::string& label) {
            const auto sign = faceSign(source, label);
            renderer.uploadModel(source);
            for (std::size_t axis = 0; axis < axes.size(); ++axis)
                expectedLight(light(axes[axis], label + "-axis-" + std::to_string(axis)), expectedNormal * sign,
                              axes[axis], label);
            return sign;
        };

        const auto source = plane();
        const auto sign = faceSign(source, "identity");
        const aeg::math::Vec3 litZ{0, 0, sign};
        renderer.uploadModel(withoutMap(source));
        const auto geometric = light(litZ, "geometric-baseline");
        renderer.uploadModel(plane(neutral));
        nearPixels(light(litZ, "neutral-linear"), geometric, 2, "linear neutral normal texture");

        response(source, mapped(tiltedX, 1, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}), "tilted-x");
        response(plane(tiltedY), mapped(tiltedY, 1, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}), "tilted-y");
        // Mirroring U reverses T and handedness, leaving B unchanged.
        response(plane(tiltedX, {0, 0, 1}, {-1, 0, 0, -1}), mapped(tiltedX, 1, {0, 0, 1}, {-1, 0, 0}, {0, 1, 0}),
                 "mirrored-uv-x");
        response(plane(tiltedY, {0, 0, 1}, {-1, 0, 0, -1}), mapped(tiltedY, 1, {0, 0, 1}, {-1, 0, 0}, {0, 1, 0}),
                 "mirrored-uv-y");

        // Four distinct normals in one texture require interpolated UV sampling.
        // The 3x3 image patches surround UV (.25/.75,.25/.75), the texel centers,
        // well inside the quad. Small bilinear contributions from nearby pixels
        // fit the 0.025 linear-light bound; a constant or swapped UV cannot pass.
        const std::array<Sample, 4> spatialSamples{{tiltedX, tiltedY, {51, 128, 230}, {128, 51, 230}}};
        auto spatial = source;
        auto& spatialTexture = spatial.materials[0].normalTexture;
        spatialTexture = {2, 2, {}};
        for (const auto sample : spatialSamples) {
            spatialTexture.rgba.insert(spatialTexture.rgba.end(), sample.begin(), sample.end());
            spatialTexture.rgba.push_back(255);
        }
        const std::array<aeg::math::Vec2, 4> texelCenters{
            {{0.3875f, 0.3f}, {0.6125f, 0.3f}, {0.3875f, 0.7f}, {0.6125f, 0.7f}}};
        const auto spatialLight = aeg::math::normalize(aeg::math::Vec3(1, 2, 3)) * sign;
        renderer.uploadModel(spatial);
        const auto originalSpatial = light(spatialLight, "spatial-original");
        auto mirroredSpatial = spatial;
        for (auto& vertex : mirroredSpatial.meshes[0].vertices) {
            vertex.uv.x = 1 - vertex.uv.x;
            vertex.tangent = {-1, 0, 0, -1};
        }
        // Reflecting U reverses physical tangent X. Reflecting the light X too
        // makes each mirrored texel retain its original brightness independently
        // of the back-face convention, while exchanging the image columns.
        const aeg::math::Vec3 reflectedLight{-spatialLight.x, spatialLight.y, spatialLight.z};
        renderer.uploadModel(mirroredSpatial);
        const auto mirroredSpatialImage = light(reflectedLight, "spatial-mirrored-uv");
        for (std::size_t i = 0; i < spatialSamples.size(); ++i) {
            const auto originalNormal = mapped(spatialSamples[i], 1, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}) * sign;
            const auto mirroredNormal = mapped(spatialSamples[i ^ 1], 1, {0, 0, 1}, {-1, 0, 0}, {0, 1, 0}) * sign;
            const double originalExpected = ambient + (1 - ambient) * aeg::math::dot(originalNormal, spatialLight);
            const double mirroredExpected = ambient + (1 - ambient) * aeg::math::dot(mirroredNormal, reflectedLight);
            require(std::abs(patchLight(originalSpatial, texelCenters[i]) - originalExpected) < 0.025,
                    "2x2 normal map sampled the wrong original UV region " + std::to_string(i));
            require(std::abs(patchLight(mirroredSpatialImage, texelCenters[i]) - mirroredExpected) < 0.025,
                    "2x2 normal map sampled the wrong mirrored UV region " + std::to_string(i));
            require(std::abs(patchLight(mirroredSpatialImage, texelCenters[i]) -
                             patchLight(originalSpatial, texelCenters[i ^ 1])) < 0.009,
                    "UV mirror/reflected light did not exchange the normal texture columns");
        }

        // Close the real importer -> generated tangent -> material upload -> GPU
        // path, including the embedded image and authored glTF normal scale.
        settings.model = aeg::math::translation(aeg::math::Vec3(-0.7f, -0.7f, 0.5f)) *
                         aeg::math::scale(aeg::math::Vec3(1.4f, 1.4f, 1));
        settings.lightDirection = -aeg::math::normalize(aeg::math::Vec3(1, 2, 1)) * sign;
        Image importedGltf;
        for (const auto* filename : {"normal-map.gltf", "normal-map.glb"}) {
            const auto imported = aeg::assets::loadModel(std::filesystem::path(AEG_TEST_FIXTURES) / filename);
            renderer.uploadModel(imported);
            settings.normalMapping = false;
            frame();
            const auto importedBaseline = capture(std::string(filename) + "-disabled");
            settings.normalMapping = true;
            frame();
            const auto importedMapped = capture(std::string(filename) + "-mapped");
            std::size_t changed{};
            for (std::size_t i = 0; i < importedMapped.rgb.size(); i += 3)
                if (std::abs(int(importedMapped.rgb[i]) - int(importedBaseline.rgb[i])) >= 4) ++changed;
            require(changed > 200, std::string(filename) + " imported normal map did not affect GPU lighting");
            if (importedGltf.rgb.empty())
                importedGltf = importedMapped;
            else
                samePixels(importedMapped, importedGltf, "equivalent glTF/GLB imported normal map");
        }
        settings.model = aeg::math::identity();

        // Every imported format and source-less procedural ModelData uses the
        // same public normal assignment path. PMX has no native normal slot;
        // these tests must visibly light the external RGB image on actual PMX
        // meshes, rather than merely remove an importer format exclusion.
        auto fitImported = [&](const aeg::assets::ModelData& model) {
            const auto center = model.bounds.center();
            const float radius = std::max(model.bounds.radius(), 0.01f);
            const auto eye = center + aeg::math::normalize(aeg::math::Vec3(2.5f, 1.8f, 3.5f)) * radius * 3.2f;
            auto projection =
                aeg::math::perspective(aeg::math::to_radians(45.0f), float(width) / float(height), radius * 0.01f, radius * 7, {true, false});
            settings.clipSpace = {true, false};
            settings.viewProjection = projection * aeg::math::look_at(eye, center, aeg::math::Vec3(0, 1, 0));
            settings.model = aeg::math::identity();
            settings.lightDirection = -aeg::math::normalize(aeg::math::Vec3(0.6f, 1, 0.8f));
        };
        const auto fixtures = std::filesystem::path(AEG_TEST_FIXTURES);
        {
            // No name heuristic can associate foreign-normal.png with this
            // PMX material. Only its explicit EMD -> FX resource chain can.
            EffectPmxFiles files;
            std::filesystem::create_directory(files.root / "pmx-textures");
            std::filesystem::create_directory(files.root / "effects");
            std::filesystem::copy_file(fixtures / "pmx-utf8.pmx", files.root / "automatic.pmx");
            std::filesystem::copy_file(fixtures / "pmx-textures" / files.baseName,
                                       files.root / "pmx-textures" / files.baseName);
            std::filesystem::copy_file(fixtures / "normal-map.png", files.root / "effects" / "foreign-normal.png");
            // The other material has an exact naming decoy. The author's
            // default 'none' must prevent automatically attaching it.
            std::filesystem::copy_file(fixtures / "normal-map.png", files.root / "Solid_n.png");
            auto text = [&](const std::filesystem::path& relative, std::string_view contents) {
                std::ofstream file(files.root / relative, std::ios::binary);
                file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
                require(bool(file), "could not author original EMD GPU fixture");
            };
            text("automatic.emd", "[Info]\nVersion = 3\n[Effect]\nObj = none\nObj[0] = effects\\Surface.fx\n");
            text("effects/Surface.fx", "#define USE_NORMALMAP\n#define TEXTURE_NORMALMAP \"foreign-normal.png\"\n"
                                       "float NormalMapResolution = 1;\n");
            auto bound = aeg::assets::loadModel(files.root / "automatic.pmx");
            constexpr std::size_t materialIndex = 0;
            require(bound.materials.size() == 2 && bound.materials[materialIndex].hasNormalMap &&
                        bound.materials[materialIndex].normalTextureDeclared &&
                        bound.materials[materialIndex].normalTextureSource ==
                            aeg::foundation::pathToUtf8(files.root / "effects" / "foreign-normal.png"),
                    "PMX import did not resolve its explicit EMD -> FX normal image");
            require(bound.materials[1].name == "Solid" && !bound.materials[1].hasNormalMap &&
                        bound.materials[1].normalTextureDeclared,
                    "EMD default none did not block the unrelated material's naming decoy");
            for (const auto& mesh : bound.meshes)
                if (mesh.materialIndex == materialIndex)
                    for (const auto& vertex : mesh.vertices)
                        require(std::abs(vertex.tangent.w) == 1, "EMD-bound PMX omitted prepared tangent frames");
            fitImported(bound);

            auto maskModel = bound;
            for (std::size_t i = 0; i < maskModel.materials.size(); ++i) {
                auto& material = maskModel.materials[i];
                material.baseColor = i == materialIndex ? aeg::math::Vec4(0, 0, 0, 1) : aeg::math::Vec4(1);
                material.texture = {};
                material.hasTexture = false;
                material.hasNormalMap = false;
            }
            renderer.uploadModel(maskModel);
            settings.unlit = true;
            frame();
            const auto otherMaterialMask = capture("emd-pmx-other-material-mask");
            settings.unlit = false;
            renderer.uploadModel(bound);
            frame();
            const auto mappedEffect = capture("emd-pmx-mapped", false);
            settings.normalMapping = false;
            frame();
            const auto effectBaseline = capture("emd-pmx-disabled", false);
            require(changedPixels(mappedEffect, effectBaseline) > 200,
                    "EMD-bound PMX normal image did not visibly change GPU lighting");
            unchangedMaterial(mappedEffect, effectBaseline, otherMaterialMask, "EMD-bound PMX normal");
            settings.normalMapping = true;

            aeg::assets::clearMaterialNormalMap(bound, materialIndex);
            aeg::assets::prepareModelMaterials(bound);
            require(!bound.materials[materialIndex].hasNormalMap,
                    "ordinary material preparation reloaded the manually cleared EMD normal");
            renderer.uploadModel(bound);
            frame();
            samePixels(capture("emd-pmx-cleared", false), effectBaseline, "manually cleared EMD normal map");
            aeg::assets::setMaterialNormalMap(bound, materialIndex,
                                              aeg::assets::ImageData{1, 1, {tiltedY[0], tiltedY[1], tiltedY[2], 255}});
            renderer.uploadModel(bound);
            frame();
            const auto manualEffect = capture("emd-pmx-manual-override", false);
            require(changedPixels(manualEffect, mappedEffect) > 200 &&
                        changedPixels(manualEffect, effectBaseline) > 200,
                    "manual image did not replace the EMD normal binding");
            unchangedMaterial(manualEffect, effectBaseline, otherMaterialMask, "manual EMD override");

            if (renderer.supportsTemporalAA()) {
                renderer.setAntialiasing({1, true, true});
                advance(3);
                require(renderer.temporalFrameCount() == 3, "EMD normal fixture did not accumulate TAA");
                bound = aeg::assets::loadModel(files.root / "automatic.pmx");
                renderer.uploadModel(bound);
                require(renderer.temporalFrameCount() == 0, "EMD normal reload retained stale TAA history");
                frame();
                const auto temporalEffect = capture("emd-pmx-taa-fxaa-mapped", false);
                aeg::assets::clearMaterialNormalMap(bound, materialIndex);
                aeg::assets::prepareModelMaterials(bound);
                renderer.uploadModel(bound);
                require(renderer.temporalFrameCount() == 0, "cleared EMD normal retained stale TAA history");
                frame();
                require(changedPixels(capture("emd-pmx-taa-fxaa-cleared", false), temporalEffect) > 200,
                        "TAA+FXAA lost the EMD-bound normal effect");
                renderer.setAntialiasing({});
            }
        }
        {
            // No material editing API is used before this import. An original
            // PMX plus its appended-N companion must acquire normal lighting
            // automatically, including Unicode and literal-percent filenames.
            AutomaticPmxFiles files;
            const auto textureDirectory = files.root / "pmx-textures";
            std::filesystem::create_directory(textureDirectory);
            std::filesystem::copy_file(fixtures / "pmx-utf8.pmx", files.root / "automatic.pmx");
            std::filesystem::copy_file(fixtures / "pmx-textures" / files.baseName, textureDirectory / files.baseName);
            std::filesystem::copy_file(fixtures / "normal-map.png", textureDirectory / files.normalName);
            auto automatic = aeg::assets::loadModel(files.root / "automatic.pmx");
            const auto materialIndex = automatic.meshes.front().materialIndex;
            const auto& normalMaterial = automatic.materials.at(materialIndex);
            require(normalMaterial.hasNormalMap && normalMaterial.normalTexture.width == 2 &&
                        normalMaterial.normalScale == 1 &&
                        normalMaterial.normalTextureSource ==
                            aeg::foundation::pathToUtf8(textureDirectory / files.normalName),
                    "PMX import did not automatically discover its appended-N normal image");
            for (std::size_t i = 0; i < automatic.materials.size(); ++i)
                if (i != materialIndex)
                    require(!automatic.materials[i].hasNormalMap,
                            "automatic discovery assigned a normal image to an unrelated material");
            for (const auto& mesh : automatic.meshes)
                if (mesh.materialIndex == materialIndex)
                    for (const auto& vertex : mesh.vertices)
                        require(std::abs(vertex.tangent.w) == 1,
                                "automatic PMX normal import omitted its generated tangent frames");
            fitImported(automatic);
            renderer.uploadModel(automatic);
            frame();
            const auto automaticMapped = capture("auto-pmx-mapped", false);
            settings.normalMapping = false;
            frame();
            const auto automaticBaseline = capture("auto-pmx-disabled", false);
            require(changedPixels(automaticMapped, automaticBaseline) > 200,
                    "automatically loaded PMX normal image did not visibly change GPU lighting");
            settings.normalMapping = true;

            aeg::assets::clearMaterialNormalMap(automatic, materialIndex);
            aeg::assets::prepareModelMaterials(automatic);
            require(!automatic.materials[materialIndex].hasNormalMap,
                    "shared preparation automatically reattached a manually cleared normal image");
            renderer.uploadModel(automatic);
            frame();
            samePixels(capture("auto-pmx-cleared", false), automaticBaseline,
                       "manually cleared automatic PMX normal map");
            aeg::assets::setMaterialNormalMap(automatic, materialIndex,
                                              aeg::assets::ImageData{1, 1, {tiltedY[0], tiltedY[1], tiltedY[2], 255}});
            renderer.uploadModel(automatic);
            frame();
            const auto overridden = capture("auto-pmx-manual-override", false);
            require(changedPixels(overridden, automaticMapped) > 200 &&
                        changedPixels(overridden, automaticBaseline) > 200,
                    "manual normal image override did not replace the discovered PMX image");

            if (renderer.supportsTemporalAA()) {
                renderer.setAntialiasing({1, true, true});
                advance(3);
                require(renderer.temporalFrameCount() == 3, "automatic PMX normal test did not accumulate TAA");
                automatic = aeg::assets::loadModel(files.root / "automatic.pmx");
                renderer.uploadModel(automatic);
                require(renderer.temporalFrameCount() == 0,
                        "reimporting an automatically mapped PMX retained stale TAA history");
                frame();
                const auto temporalAutomatic = capture("auto-pmx-taa-fxaa-mapped", false);
                aeg::assets::clearMaterialNormalMap(automatic, materialIndex);
                aeg::assets::prepareModelMaterials(automatic);
                renderer.uploadModel(automatic);
                require(renderer.temporalFrameCount() == 0,
                        "clearing an automatically mapped PMX retained stale TAA history");
                frame();
                require(changedPixels(capture("auto-pmx-taa-fxaa-cleared", false), temporalAutomatic) > 200,
                        "TAA+FXAA lost the automatically discovered PMX normal effect");
                renderer.setAntialiasing({});
            }
        }
        for (const auto* filename :
             {"pmx-utf8.pmx", "pmx-utf16.pmx", "normal-height.obj", "normal-map.gltf", "normal-map.glb"}) {
            auto model = aeg::assets::loadModel(fixtures / filename);
            const auto materialIndex = model.meshes.front().materialIndex;
            aeg::assets::clearMaterialNormalMap(model, materialIndex);
            fitImported(model);
            renderer.uploadModel(model);
            frame();
            const auto baseline = capture(std::string(filename) + "-external-baseline", false);

            const bool pmx = std::filesystem::path(filename).extension() == ".pmx";
            Image otherMaterialMask;
            if (pmx) {
                require(model.materials.size() == 2, "PMX fixture must retain its two native materials");
                auto maskModel = model;
                for (std::size_t i = 0; i < maskModel.materials.size(); ++i) {
                    auto& material = maskModel.materials[i];
                    material.baseColor = i == materialIndex ? aeg::math::Vec4(0, 0, 0, 1) : aeg::math::Vec4(1);
                    material.texture = {};
                    material.hasTexture = false;
                    material.hasNormalMap = false;
                }
                renderer.uploadModel(maskModel);
                settings.unlit = true;
                frame();
                otherMaterialMask = capture(std::string(filename) + "-other-material-mask");
                settings.unlit = false;
            }

            // Relative file paths are literal and resolved by the common API,
            // identically for PMX, OBJ, glTF and GLB.
            aeg::assets::setMaterialNormalMap(model, materialIndex, std::filesystem::path("normal-map.png"));
            for (const auto& mesh : model.meshes)
                if (mesh.materialIndex == materialIndex)
                    for (const auto& vertex : mesh.vertices)
                        require(std::abs(vertex.tangent.w) == 1, "shared material preparation omitted a tangent");
            renderer.uploadModel(model);
            frame();
            const auto assigned = capture(std::string(filename) + "-external-assigned", false);
            require(changedPixels(assigned, baseline) > 200,
                    std::string(filename) + " external normal map did not visibly affect imported model lighting");
            if (pmx) unchangedMaterial(assigned, baseline, otherMaterialMask, filename);

            settings.normalMapping = false;
            frame();
            samePixels(capture(std::string(filename) + "-external-disabled", false), baseline,
                       "shared normal assignment disable");
            settings.normalMapping = true;
            aeg::assets::setMaterialNormalMap(model, materialIndex,
                                              aeg::assets::ImageData{1, 1, {tiltedY[0], tiltedY[1], tiltedY[2], 255}});
            renderer.uploadModel(model);
            frame();
            const auto replaced = capture(std::string(filename) + "-external-replaced", false);
            require(changedPixels(replaced, assigned) > 200, "normal image replacement retained the old GPU map");
            if (pmx) unchangedMaterial(replaced, baseline, otherMaterialMask, filename);

            if (std::string_view(filename) == "pmx-utf8.pmx" && renderer.supportsTemporalAA()) {
                renderer.setAntialiasing({1, true, true});
                advance(3);
                require(renderer.temporalFrameCount() == 3, "externally mapped PMX did not accumulate TAA");
                aeg::assets::setMaterialNormalMap(model, materialIndex, std::filesystem::path("normal-map.png"));
                renderer.uploadModel(model);
                require(renderer.temporalFrameCount() == 0, "PMX normal replacement retained stale TAA history");
                frame();
                require(renderer.temporalFrameCount() == 1, "PMX replacement did not start a fresh temporal frame");
                const auto temporalMapped = capture("pmx-external-taa-fxaa-mapped", false);
                settings.normalMapping = false;
                frame();
                require(renderer.temporalFrameCount() == 1, "PMX normal switch did not reset TAA");
                require(changedPixels(capture("pmx-external-taa-fxaa-disabled", false), temporalMapped) > 200,
                        "TAA+FXAA lost the externally assigned PMX normal map");
                settings.normalMapping = true;
                renderer.setAntialiasing({});
            }

            aeg::assets::clearMaterialNormalMap(model, materialIndex);
            renderer.uploadModel(model);
            frame();
            samePixels(capture(std::string(filename) + "-external-cleared", false), baseline,
                       "cleared external normal assignment");
        }
        settings.model = settings.viewProjection = aeg::math::identity();
        auto procedural = withoutMap(source);
        for (auto& vertex : procedural.meshes[0].vertices) vertex.tangent = aeg::math::Vec4(0);
        require(procedural.source.empty(), "procedural normal fixture unexpectedly has a source format");
        aeg::assets::setMaterialNormalMap(procedural, 0,
                                          aeg::assets::ImageData{1, 1, {tiltedX[0], tiltedX[1], tiltedX[2], 255}});
        response(procedural, mapped(tiltedX, 1, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}), "procedural-common-material");

        settings.lightDirection = -aeg::math::normalize(aeg::math::Vec3(sign, 0, sign));
        renderer.uploadModel(withoutMap(source));
        frame();
        const auto disabledBaseline = capture("controls-baseline");
        renderer.uploadModel(source);
        settings.normalMapping = false;
        frame();
        samePixels(capture("disabled"), disabledBaseline, "disabled normal mapping");
        settings.normalMapping = true;
        settings.normalStrength = 0;
        frame();
        samePixels(capture("global-zero"), disabledBaseline, "zero global normal strength");
        settings.normalStrength = 1;
        renderer.uploadModel(plane(tiltedX, {0, 0, 1}, {1, 0, 0, 1}, 0));
        frame();
        samePixels(capture("material-zero"), disabledBaseline, "zero material normal scale");
        response(plane(tiltedX, {0, 0, 1}, {1, 0, 0, 1}, 2), mapped(tiltedX, 2, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}),
                 "material-scale-two");
        response(plane(tiltedX, {0, 0, 1}, {1, 0, 0, 1}, -1), mapped(tiltedX, -1, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}),
                 "material-scale-negative");
        renderer.uploadModel(plane(tiltedX, {0, 0, 1}, {1, 0, 0, 1}, 2));
        const auto materialTwo = light(litZ, "material-two-z");
        renderer.uploadModel(source);
        settings.normalStrength = 2;
        samePixels(light(litZ, "global-two-z"), materialTwo, "global/material strength equivalence");
        settings.normalStrength = 1;

        // Nonuniform scale: N = normalize(1/1.8,1/.65,1/.45),
        // T = normalize(1.8,-.65,0), B = cross(N,T). A wrongly inverse-
        // transposed tangent differs even after orthogonalization here.
        const auto diagonal =
            plane(tiltedX, aeg::math::normalize(aeg::math::Vec3(1, 1, 1)), aeg::math::Vec4(aeg::math::normalize(aeg::math::Vec3(1, -1, 0)), 1));
        const aeg::math::Vec3 scaledN{0.20133877f, 0.55755351f, 0.80535507f};
        const aeg::math::Vec3 scaledT{0.94055390f, -0.33964446f, 0};
        const aeg::math::Vec3 scaledB{0.27353439f, 0.75747985f, -0.59279272f};
        settings.model = aeg::math::scale(aeg::math::Vec3(1.8f, 0.65f, 0.45f));
        response(diagonal, mapped(tiltedX, 1, scaledN, scaledT, scaledB), "nonuniform-model");
        settings.model = aeg::math::scale(aeg::math::Vec3(-1.8f, 0.65f, 0.45f));
        response(diagonal,
                 mapped(tiltedX, 1, {-scaledN.x, scaledN.y, scaledN.z}, {-scaledT.x, scaledT.y, scaledT.z},
                        {-scaledB.x, scaledB.y, scaledB.z}),
                 "mirrored-nonuniform-model");
        // Y tilt isolates determinant correction of B under a model mirror.
        settings.model = aeg::math::scale(aeg::math::Vec3(-1, 0.75f, 1));
        response(plane(tiltedY), mapped(tiltedY, 1, {0, 0, 1}, {-1, 0, 0}, {0, 1, 0}), "mirrored-model-y");
        settings.model = aeg::math::identity();

        renderer.uploadModel(withoutMap(source));
        const auto fallback = light(litZ, "invalid-basis-baseline");
        const std::array<aeg::math::Vec4, 3> invalidTangents{{{0, 0, 0, 0}, {0, 0, 0, 1}, {0, 0, 1, 1}}};
        for (std::size_t i = 0; i < invalidTangents.size(); ++i) {
            auto invalidBasis = plane(tiltedX, {0, 0, 1}, invalidTangents[i]);
            for (auto& vertex : invalidBasis.meshes[0].vertices) vertex.uv = {0, 0};
            renderer.uploadModel(invalidBasis);
            samePixels(light(litZ, "invalid-basis-" + std::to_string(i)), fallback, "degenerate UV/tangent fallback");
        }
        settings.unlit = true;
        renderer.uploadModel(withoutMap(source));
        frame();
        const auto unlit = capture("unlit-baseline");
        renderer.uploadModel(source);
        frame();
        samePixels(capture("unlit-mapped"), unlit, "unlit normal map");
        settings.normalStrength = 4;
        frame();
        samePixels(capture("unlit-strength-four"), unlit, "unlit normal strength");
        settings.unlit = false;
        settings.normalStrength = 1;

        // Check mapped interiors through every requested MSAA/FXAA path.
        // Merely softening primitive edges cannot satisfy these assertions.
        const auto expectedNormal = mapped(tiltedX, 1, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}) * sign;
        const aeg::math::Vec3 litX{sign, 0, 0};
        auto mixed = source;
        mixed.meshes.push_back(mixed.meshes[0]);
        mixed.meshes[1].materialIndex = 1;
        mixed.materials.push_back(withoutMap(source).materials[0]);
        for (auto& vertex : mixed.meshes[0].vertices) vertex.position.x = vertex.position.x * 0.65f - 0.5f;
        for (auto& vertex : mixed.meshes[1].vertices) vertex.position.x = vertex.position.x * 0.65f + 0.5f;
        renderer.uploadModel(mixed);
        const auto mixedImage = light(litX, "mixed-materials");
        expectedLight(mixedImage, expectedNormal, litX, "mapped material beside fallback", 0.25f);
        expectedLight(mixedImage, {0, 0, sign}, litX, "fallback material beside mapped", 0.75f);
        renderer.uploadModel(source);
        for (const auto samples : {1u, 2u, 4u, 8u})
            for (const bool fxaa : {false, true}) {
                renderer.setAntialiasing({samples, fxaa});
                const auto name = "aa-" + std::to_string(samples) + (fxaa ? "-fxaa" : "-plain");
                expectedLight(light(litX, name), expectedNormal, litX, name);
                require(!renderer.antialiasing().taa, "spatial AA unexpectedly enabled TAA");
            }
        if (renderer.supportsTemporalAA()) {
            for (const bool fxaa : {false, true}) {
                renderer.setAntialiasing({4, fxaa, true});
                require(renderer.antialiasing() == aeg::render::AntialiasingSettings{1, fxaa, true},
                        "normal-mapped TAA did not force 1x MSAA");
                require(renderer.temporalFrameCount() == 0, "AA change retained normal-map history");
                settings.lightDirection = -litX;
                advance(4);
                require(renderer.temporalFrameCount() == 4, "normal-mapped history did not accumulate");
                const auto name = fxaa ? "taa-fxaa" : "taa";
                expectedLight(capture(name), expectedNormal, litX, name);
                settings.normalMapping = false;
                frame();
                require(renderer.temporalFrameCount() == 1, "normal-map disable did not reset TAA history");
                expectedLight(capture(std::string(name) + "-disabled"), {0, 0, sign}, litX, "TAA disabled map");
                frame();
                require(renderer.temporalFrameCount() == 2, "unchanged normal controls reset TAA history");
                settings.normalMapping = true;
                frame();
                require(renderer.temporalFrameCount() == 1, "normal-map enable did not reset TAA history");
                expectedLight(capture(std::string(name) + "-enabled"), expectedNormal, litX, "TAA enabled map");
                advance(2);
                settings.normalStrength = 0;
                frame();
                require(renderer.temporalFrameCount() == 1, "zero normal strength did not reset TAA history");
                expectedLight(capture(std::string(name) + "-zero"), {0, 0, sign}, litX, "TAA zero strength");
                frame();
                settings.normalStrength = 0.5f;
                frame();
                require(renderer.temporalFrameCount() == 1, "positive normal strength did not reset TAA history");
                const auto halfNormal = mapped(tiltedX, 0.5f, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}) * sign;
                expectedLight(capture(std::string(name) + "-half"), halfNormal, litX, "TAA half strength");
                settings.normalStrength = 1;
                frame();
                require(renderer.temporalFrameCount() == 1, "restoring normal strength retained TAA history");
            }
        } else {
            std::cout << "TAA formats unavailable; spatial normal-map paths verified\n";
        }

        renderer.setAntialiasing({});
        settings.normalMapping = true;
        settings.normalStrength = 1;
        settings.lightDirection = -litX;
        frame();
        const auto retained = capture("before-reload");
        auto invalid = source;
        invalid.meshes[0].indices[0] = 0xffffffffu;
        bool rejected{};
        try {
            renderer.uploadModel(invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "invalid normal-mapped replacement was accepted");
        samePixels(capture("rejected-reload"), retained, "failed normal-mapped replacement");
        renderer.uploadModel(withoutMap(source));
        frame();
        expectedLight(capture("reload-unmapped"), {0, 0, sign}, litX, "unmapped replacement");
        renderer.uploadModel(source);
        frame();
        samePixels(capture("reload-mapped"), retained, "normal-map reload");

        constexpr std::array<std::array<std::uint32_t, 2>, 6> sizes{
            {{1, 1}, {1, 37}, {41, 1}, {3, 5}, {93, 71}, {319, 181}}};
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            const bool temporal = renderer.supportsTemporalAA() && i % 2 == 1;
            renderer.setAntialiasing({4, i % 2 == 0, temporal});
            width = sizes[i][0];
            height = sizes[i][1];
            renderer.resizeViewport(width, height);
            require(renderer.temporalFrameCount() == 0, "normal-map viewport resize retained history");
            bool staleRejected{};
            try {
                renderer.captureViewport(output / "stale.ppm");
            } catch (const std::exception&) {
                staleRejected = true;
            }
            require(staleRejected, "normal-map capture accepted a resized unrendered target");
            glfwSetWindowSize(window, 350 + int(i % 3) * 113, 270 + int(i % 4) * 59);
            advance(2);
            const auto image = capture("resized-" + std::to_string(i));
            if (width >= 30 && height >= 30) expectedLight(image, expectedNormal, litX, "resized mapped output");
        }
        renderer.setAntialiasing({});
        width = 321;
        height = 239;
        frame();
        samePixels(capture("normal-final"), retained, "normal-map resize/AA round trip");
        renderer.waitIdle();
        require(renderer.validationErrorCount() == 0, "Vulkan normal-map lifecycle validation failed");
    } catch (const std::exception& error) {
        std::cout << "FAIL normal mapping: " << error.what() << '\n';
        result = 1;
    }
    // Renderer destruction precedes this check, including descriptor/image teardown.
    std::cerr.rdbuf(previous);
    if (diagnostics.str().find("[Vulkan error]") != std::string::npos) {
        std::cerr << diagnostics.str();
        result = 1;
    }
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    if (!result)
        std::cout << "PASS linear neutral/tilted normals, spatial/mirrored UVs, automatic/EMD-bound PMX normals, "
                     "glTF/GLB import, external "
                     "PMX/OBJ/glTF/GLB and procedural maps, material/global "
                     "strength, transformed/mirrored frames, "
                     "degenerate fallback, unlit, AA/TAA resets, reload/resize/final capture and clean teardown\n";
    return result;
}
