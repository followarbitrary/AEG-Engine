#include "RendererHarness.hpp"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
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
            "invalid TAA capture header: " + path.string());
    image.rgb.resize(static_cast<std::size_t>(image.width) * image.height * 3);
    input.read(reinterpret_cast<char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    require(input.gcount() == static_cast<std::streamsize>(image.rgb.size()) &&
                input.peek() == std::char_traits<char>::eof(),
            "incorrect TAA capture size");
    return image;
}
std::size_t intermediate(const Image& image) {
    std::size_t count{};
    for (std::size_t i = 0; i < image.rgb.size(); i += 3) {
        require(image.rgb[i] == image.rgb[i + 1] && image.rgb[i] == image.rgb[i + 2],
                "grayscale fixture acquired a color tint");
        if (image.rgb[i] > 1 && image.rgb[i] < 254) ++count;
    }
    return count;
}
// Check only pixels surrounded by a constant reference neighborhood. This allows
// the basic filter's edge softness while detecting trails in newly revealed flats.
void flatInteriors(const Image& image, const Image& reference) {
    require(image.width == reference.width && image.height == reference.height, "stale capture extent");
    std::size_t checked{};
    constexpr int margin = 4;
    for (int y = margin; y < int(image.height) - margin; ++y)
        for (int x = margin; x < int(image.width) - margin; ++x) {
            const auto at = (static_cast<std::size_t>(y) * image.width + x) * 3;
            bool flat = true;
            for (int dy = -margin; dy <= margin && flat; ++dy)
                for (int dx = -margin; dx <= margin; ++dx)
                    if (reference.rgb[(static_cast<std::size_t>(y + dy) * image.width + x + dx) * 3] !=
                        reference.rgb[at]) {
                        flat = false;
                        break;
                    }
            if (flat) {
                require(std::abs(int(image.rgb[at]) - int(reference.rgb[at])) <= 2,
                        "temporal history changed a flat interior or left a disocclusion trail");
                ++checked;
            }
        }
    require(checked > 1000, "fixture has insufficient flat interior pixels");
}
double variation(const std::vector<Image>& images) {
    double difference{};
    for (std::size_t frame = 0; frame < images.size(); ++frame) {
        const auto& a = images[frame].rgb;
        const auto& b = images[(frame + 1) % images.size()].rgb;
        for (std::size_t i = 0; i < a.size(); i += 3) difference += std::abs(int(a[i]) - int(b[i]));
    }
    return difference / double(images.size());
}
float halton(unsigned index, unsigned base) {
    float result{}, fraction = 1;
    for (; index; index /= base) {
        fraction /= float(base);
        result += fraction * float(index % base);
    }
    return result;
}
glm::mat4 shift(float pixels, std::uint32_t width) {
    return glm::translate(glm::mat4(1), glm::vec3(2 * pixels / float(width), 0, 0));
}
Image shifted(const Image& source, int pixels) {
    auto image = source;
    std::fill(image.rgb.begin(), image.rgb.end(), 0);
    for (std::uint32_t y = 0; y < image.height; ++y)
        for (int x = 0; x < int(image.width); ++x) {
            const int from = x - pixels;
            if (from >= 0 && from < int(image.width))
                for (int c = 0; c < 3; ++c)
                    image.rgb[(static_cast<std::size_t>(y) * image.width + x) * 3 + c] =
                        source.rgb[(static_cast<std::size_t>(y) * image.width + from) * 3 + c];
        }
    return image;
}
aeg::assets::ModelData triangle(bool overlap = false) {
    aeg::assets::ModelData model;
    model.name = "Temporal coverage and disocclusion fixture";
    aeg::assets::MeshData mesh;
    mesh.vertices = {{{-0.81f, -0.73f, 0.5f}, {0, 0, 1}, {}},
                     {{0.77f, -0.41f, 0.5f}, {0, 0, 1}, {}},
                     {{-0.43f, 0.83f, 0.5f}, {0, 0, 1}, {}}};
    mesh.indices = {0, 1, 2};
    model.materials.emplace_back();
    model.meshes.push_back(std::move(mesh));
    if (overlap) {
        // A nearer white triangle moves across a wide gray rear plane. The rear
        // remains visible after translation, exercising foreground disocclusion.
        for (auto& vertex : model.meshes[0].vertices) {
            vertex.position *= 0.6f;
            vertex.position.z = 0.2f;
        }
        aeg::assets::MeshData rear;
        rear.vertices = {{{-3, -3, 0.8f}, {0, 0, 1}, {}},
                         {{3, -3, 0.8f}, {0, 0, 1}, {}},
                         {{3, 3, 0.8f}, {0, 0, 1}, {}},
                         {{-3, 3, 0.8f}, {0, 0, 1}, {}}};
        rear.indices = {0, 1, 2, 0, 2, 3};
        rear.materialIndex = 1;
        model.meshes.push_back(std::move(rear));
        model.materials.emplace_back().baseColor = {0.25f, 0.25f, 0.25f, 1};
    }
    return model;
}
aeg::assets::ModelData ramp() {
    aeg::assets::ModelData model;
    aeg::assets::MeshData mesh;
    mesh.vertices = {{{-1, -0.9f, 0.5f}, {0, 0, 1}, {0, 0}},
                     {{1, -0.9f, 0.5f}, {0, 0, 1}, {1, 0}},
                     {{1, 0.9f, 0.5f}, {0, 0, 1}, {1, 1}},
                     {{-1, 0.9f, 0.5f}, {0, 0, 1}, {0, 1}}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    model.meshes.push_back(std::move(mesh));
    auto& material = model.materials.emplace_back();
    material.hasTexture = true;
    material.texture.width = 512;
    material.texture.rgba.resize(512 * 4);
    for (unsigned x = 0; x < 512; ++x) {
        const auto value = static_cast<unsigned char>(48 + 160 * std::min(x % 128, 128 - x % 128) / 64);
        for (unsigned c = 0; c < 4; ++c) material.texture.rgba[x * 4 + c] = c == 3 ? 255 : value;
    }
    return model;
}
double rampError(const Image& actual, const Image& reference) {
    double error{};
    std::size_t count{};
    for (unsigned y = reference.height / 3; y < reference.height * 2 / 3; ++y)
        for (unsigned x = 32; x < reference.width - 32; ++x) {
            const auto at = (static_cast<std::size_t>(y) * reference.width + x) * 3;
            const int left = reference.rgb[at - 6], center = reference.rgb[at], right = reference.rgb[at + 6];
            // Restrict comparison to linear slopes, away from triangle-wave turns.
            if (std::abs(left + right - 2 * center) > 2 || std::abs(right - left) < 8) continue;
            error += std::abs(int(actual.rgb[at]) - center);
            ++count;
        }
    require(count > 1000, "ramp fixture has insufficient measurable slope pixels");
    return error / double(count);
}
}

int main(int argc, char** argv) {
    if (argc != 3 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(800, 600, "AEG temporal AA test", nullptr, nullptr);
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
        const std::filesystem::path runtime(argv[1]), output(argv[2]);
        std::filesystem::create_directories(output);
        aeg::tests::TestRenderer renderer(window, runtime / "shaders", true);
        require(!renderer.antialiasing().taa && renderer.temporalFrameCount() == 0, "TAA must default off");
        aeg::render::RenderSettings settings;
        settings.unlit = true;
        settings.background = {0, 0, 0, 1};
        std::uint32_t width = 321, height = 239;
        ImTextureID viewportId{};
        auto frame = [&] {
            bool drew{};
            for (int retry = 0; retry < 8 && !drew; ++retry) {
                glfwPollEvents();
                renderer.drawFrame(settings, [&] {
                    renderer.resizeViewport(width, height);
                    ImGui::SetNextWindowPos({20, 30}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize({510, 410}, ImGuiCond_Always);
                    ImGui::Begin("Temporal scaled viewport");
                    const auto currentId = renderer.viewportTexture();
                    require(!viewportId || viewportId == currentId,
                            "TAA ping-pong or viewport replacement changed the stable GUI texture ID");
                    viewportId = currentId;
                    ImGui::Image(currentId, {457, 337});
                    ImGui::End();
                    drew = true;
                });
            }
            require(drew, "no frame submitted after resize");
        };
        auto advance = [&](unsigned count) {
            for (unsigned i = 0; i < count; ++i) {
                const auto before = renderer.temporalFrameCount();
                frame();
                require(renderer.temporalFrameCount() == before + 1, "TAA did not retain/advance submitted history");
            }
        };
        auto capture = [&](const std::string& name) {
            const auto path = output / (name + ".ppm");
            renderer.captureViewport(path);
            auto image = readPpm(path);
            require(image.width == width && image.height == height, "capture has stale viewport dimensions");
            return image;
        };
        const auto source = triangle();
        renderer.uploadModel(source);
        frame();
        const auto baseline = capture("off");
        require(intermediate(baseline) == 0, "unfiltered triangle must have binary coverage");
        if (!renderer.supportsTemporalAA()) {
            bool rejected{};
            try {
                renderer.setAntialiasing({1, false, true});
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected && !renderer.antialiasing().taa, "unsupported TAA was silently accepted");
            require(capture("unsupported").rgb == baseline.rgb, "unsupported TAA invalidated the displayed scene");
            std::cout << "TAA formats unavailable; verified explicit rejection and retained output\n";
        } else {
            std::vector<Image> raw;
            for (unsigned i = 1; i <= 8; ++i) {
                settings.viewProjection =
                    glm::translate(glm::mat4(1), glm::vec3(2 * (halton(i, 2) - 0.5f) / float(width),
                                                           2 * (halton(i, 3) - 0.5f) / float(height), 0));
                frame();
                raw.push_back(capture("raw-jitter-" + std::to_string(i)));
            }
            settings.viewProjection = glm::mat4(1);
            renderer.setAntialiasing({4, false, true});
            require(renderer.antialiasing() == aeg::render::AntialiasingSettings{1, false, true},
                    "TAA did not force 1x MSAA");
            require(renderer.temporalFrameCount() == 0, "AA activation retained stale history");
            advance(1);
            const auto first = capture("first");
            require(intermediate(first) == 0, "first TAA frame unexpectedly used spatial/history filtering");
            advance(23);
            const auto accumulated = capture("accumulated");
            const auto coverage = intermediate(accumulated);
            require(accumulated.rgb != first.rgb && coverage >= 8 && coverage < baseline.rgb.size() / 24,
                    "temporal accumulation did not produce bounded intermediate edge coverage");
            flatInteriors(accumulated, baseline);
            std::vector<Image> settled;
            for (int i = 0; i < 8; ++i) {
                advance(1);
                settled.push_back(capture("settled-" + std::to_string(i)));
            }
            const auto rawVariation = variation(raw), temporalVariation = variation(settled);
            std::cout << "TAA metrics: " << coverage << " edge pixels; raw/settled variation " << rawVariation << '/'
                      << temporalVariation << '\n';
            require(rawVariation > 0 && temporalVariation < rawVariation * 0.98,
                    "settled TAA did not reduce variation versus raw subpixel jitter");
            const auto displayed = settled.back();
            renderer.resetTemporalHistory();
            require(renderer.temporalFrameCount() == 0 && capture("reset-retained").rgb == displayed.rgb,
                    "manual reset invalidated the displayed capture");
            advance(1);
            require(capture("reset-first").rgb == first.rgb,
                    "manual reset did not restart deterministic jitter/history");
            advance(7);
            settings.viewProjection = shift(4, width);
            advance(1);
            const auto cameraMoved = capture("camera-motion");
            flatInteriors(cameraMoved, shifted(baseline, 4));
            settings.model = shift(-2, width);
            advance(1);
            const auto modelMoved = capture("model-motion");
            flatInteriors(modelMoved, shifted(baseline, 2));
            require(cameraMoved.rgb != modelMoved.rgb, "model motion left a frozen temporal output");
            auto invalid = source;
            invalid.meshes[0].indices[0] = 0xffffffffu;
            const auto retainedCount = renderer.temporalFrameCount();
            bool rejected{};
            try {
                renderer.uploadModel(invalid);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected && renderer.temporalFrameCount() == retainedCount &&
                        capture("failed-upload").rgb == modelMoved.rgb,
                    "failed upload lost temporal/display state");
            renderer.uploadModel(source);
            require(renderer.temporalFrameCount() == 0, "successful upload did not invalidate history");
            settings.model = settings.viewProjection = glm::mat4(1);
            advance(24);
            require(capture("upload-reset").rgb == accumulated.rgb, "successful upload did not restart the sequence");
            renderer.setAntialiasing({4, true, true});
            require(renderer.antialiasing() == aeg::render::AntialiasingSettings{1, true, true} &&
                        renderer.temporalFrameCount() == 0,
                    "FXAA/TAA mode change did not reset or force 1x");
            advance(24);
            const auto combined = capture("taa-fxaa");
            require(combined.rgb != accumulated.rgb && intermediate(combined) >= 8,
                    "capture omitted final FXAA after TAA");
            flatInteriors(combined, baseline);
            settings.unlit = false;
            frame();
            require(renderer.temporalFrameCount() == 1, "shading discontinuity did not reset history");
            settings.unlit = true;
            frame();
            advance(1);
            settings.background = {0.1f, 0.1f, 0.1f, 1};
            frame();
            require(renderer.temporalFrameCount() == 1, "background discontinuity did not reset history");
            advance(1);
            settings.viewProjection = glm::perspective(0.9f, float(width) / float(height), 0.1f, 10.0f) *
                                      glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
            frame();
            require(renderer.temporalFrameCount() == 1, "projection type change did not reset history");
            settings.viewProjection = glm::mat4(1);
            frame();
            require(renderer.temporalFrameCount() == 1, "return to orthographic did not reset history");
            settings.background = {0, 0, 0, 1};
            renderer.setAntialiasing({});
            frame();
            require(renderer.temporalFrameCount() == 0 && capture("returned-off").rgb == baseline.rgb,
                    "disabling TAA did not restore the original 1x pixels");
            // Flat clamping alone hides missing velocity. On a ramp, incorrect
            // velocity leaves a measurable one-pixel history lag inside the 3x3 clamp.
            renderer.uploadModel(ramp());
            renderer.setAntialiasing({1, false, true});
            advance(32);
            const auto rampStatic = capture("ramp-static");
            settings.viewProjection = shift(4, width);
            advance(1);
            const auto cameraError = rampError(capture("ramp-camera"), shifted(rampStatic, 4));
            settings.model = shift(-2, width);
            advance(1);
            const auto modelError = rampError(capture("ramp-model"), shifted(rampStatic, 2));
            std::cout << "TAA reprojection mean errors: camera " << cameraError << ", model " << modelError << '\n';
            require(cameraError < 0.75 && modelError < 0.75, "camera/model reprojection left a textured history lag");
            const auto projection = glm::perspective(0.9f, float(width) / float(height), 0.1f, 10.0f);
            const auto perspective = projection * glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0), glm::vec3(0, 1, 0));
            settings.model = glm::mat4(1);
            settings.viewProjection = perspective;
            frame();
            require(renderer.temporalFrameCount() == 1, "perspective ramp did not reset orthographic history");
            advance(31);
            const auto perspectiveStatic = capture("ramp-perspective-static");
            settings.viewProjection = shift(4, width) * perspective;
            advance(1);
            const auto perspectiveCameraError =
                rampError(capture("ramp-perspective-camera"), shifted(perspectiveStatic, 4));
            // At z=0.5 the camera gives clipW=1.5; convert a -2px shift to world x.
            settings.model =
                glm::translate(glm::mat4(1), glm::vec3(-4.0f * 1.5f / (float(width) * projection[0][0]), 0, 0));
            advance(1);
            const auto perspectiveModelError =
                rampError(capture("ramp-perspective-model"), shifted(perspectiveStatic, 2));
            std::cout << "TAA perspective reprojection mean errors: camera " << perspectiveCameraError << ", model "
                      << perspectiveModelError << '\n';
            require(perspectiveCameraError < 0.75 && perspectiveModelError < 0.75,
                    "perspective camera/model reprojection left a textured history lag");
            settings.model = settings.viewProjection = glm::mat4(1);
            renderer.setAntialiasing({});
            renderer.uploadModel(triangle(true));
            frame();
            const auto overlapBefore = capture("overlap-off");
            renderer.setAntialiasing({1, false, true});
            advance(16);
            settings.model = shift(64, width);
            advance(1);
            const auto revealed = capture("overlap-motion");
            renderer.setAntialiasing({});
            frame();
            const auto revealReference = capture("overlap-reference");
            flatInteriors(revealed, revealReference);
            std::size_t revealedPixels{};
            for (std::size_t i = 0; i < revealed.rgb.size(); i += 3)
                if ((i / 3) % width < width / 2 - 40 && overlapBefore.rgb[i] == 255 && revealReference.rgb[i] > 1 &&
                    revealReference.rgb[i] < 254) {
                    require(std::abs(int(revealed.rgb[i]) - int(revealReference.rgb[i])) <= 2,
                            "overlapping foreground removal retained a ghost");
                    ++revealedPixels;
                }
            require(revealedPixels > 100, "overlapping fixture did not reveal enough rear surface");
            constexpr std::array<std::array<std::uint32_t, 2>, 6> sizes{
                {{1, 1}, {1, 37}, {41, 1}, {3, 5}, {93, 71}, {319, 181}}};
            for (const auto size : sizes)
                for (const bool fxaa : {false, true}) {
                    renderer.setAntialiasing({4, fxaa, true});
                    advance(2);
                    const bool changed = width != size[0] || height != size[1];
                    width = size[0];
                    height = size[1];
                    renderer.resizeViewport(width, height);
                    require(renderer.temporalFrameCount() == (changed ? 0u : 2u), "resize reset behavior is wrong");
                    advance(2);
                    intermediate(capture("tiny-" + std::to_string(width) + '-' + std::to_string(height) +
                                         (fxaa ? "-fxaa" : "")));
                    renderer.setAntialiasing({});
                    frame();
                    require(renderer.temporalFrameCount() == 0, "repeated TAA toggle retained history");
                }
        }
        renderer.waitIdle();
        require(renderer.validationErrorCount() == 0, "Vulkan temporal lifecycle validation failed");
    } catch (const std::exception& error) {
        std::cout << "FAIL temporal AA: " << error.what() << '\n';
        result = 1;
    }
    std::cerr.rdbuf(previous);
    if (diagnostics.str().find("[Vulkan error]") != std::string::npos) {
        std::cerr << diagnostics.str();
        result = 1;
    }
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    if (!result)
        std::cout << "PASS temporal accumulation, motion/disocclusion, resets, final FXAA, tiny extents and teardown\n";
    return result;
}
