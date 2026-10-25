#include <aeg/VulkanRenderer.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdlib>
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
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Image {
    std::uint32_t width{}, height{};
    std::vector<unsigned char> rgb;
};

Image readPpm(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "missing AA capture: " + path.string());
    std::string magic;
    unsigned maximum = 0;
    Image image;
    input >> magic >> image.width >> image.height >> maximum;
    require(magic == "P6" && maximum == 255 && image.width && image.height,
            "invalid AA PPM header: " + path.string());
    require(input.get() == '\n', "missing AA PPM header terminator");
    image.rgb.resize(static_cast<std::size_t>(image.width) * image.height * 3);
    input.read(reinterpret_cast<char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    require(input.gcount() == static_cast<std::streamsize>(image.rgb.size()) &&
                input.peek() == std::char_traits<char>::eof(),
            "incorrect AA PPM pixel count");
    return image;
}

void sameExtent(const Image& actual, const Image& expected) {
    require(actual.width == expected.width && actual.height == expected.height,
            "capture used the wrong viewport extent");
}

void stableFlatImage(const Image& actual, const Image& expected) {
    sameExtent(actual, expected);
    for (std::size_t i = 0; i < actual.rgb.size(); ++i)
        require(std::abs(int(actual.rgb[i]) - int(expected.rgb[i])) <= 1,
                "AA changed an empty, constant-color scene");
}

std::size_t intermediatePixels(const Image& image) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < image.rgb.size(); i += 3) {
        require(image.rgb[i] == image.rgb[i + 1] && image.rgb[i] == image.rgb[i + 2],
                "white/black fixture acquired a color tint");
        if (image.rgb[i] > 1 && image.rgb[i] < 254) ++count;
    }
    return count;
}

void binaryTriangle(const Image& image) {
    require(intermediatePixels(image) == 0, "AA Off introduced intermediate edge pixels");
    const auto pixels = image.rgb.size() / 3;
    const auto white = std::count(image.rgb.begin(), image.rgb.end(), static_cast<unsigned char>(255)) / 3;
    const auto black = std::count(image.rgb.begin(), image.rgb.end(), static_cast<unsigned char>(0)) / 3;
    require(static_cast<std::size_t>(white) > pixels / 8 && static_cast<std::size_t>(black) > pixels / 3,
            "synthetic triangle is absent or fills the scene");
}

// A missing resolve, a no-op FXAA pass, capture of the pre-FXAA image, and
// filtering the whole image must all fail these observable pixel assertions.
void antialiasedTriangle(const Image& image, const Image& baseline, const std::string& mode) {
    sameExtent(image, baseline);
    require(image.rgb != baseline.rgb, mode + " did not change final captured scene pixels");
    const auto intermediate = intermediatePixels(image);
    require(intermediate >= std::max<std::uint32_t>(8, std::min(image.width, image.height) / 8),
            mode + " did not produce intermediate edge coverage");
    require(intermediate < image.rgb.size() / 30, mode + " blurred too much of the scene");

    std::size_t whiteInterior = 0, blackInterior = 0;
    constexpr int margin = 6;
    for (int y = margin; y < int(image.height) - margin; ++y) {
        for (int x = margin; x < int(image.width) - margin; ++x) {
            const auto at = (static_cast<std::size_t>(y) * image.width + x) * 3;
            const auto value = baseline.rgb[at];
            bool interior = true;
            for (int dy = -margin; dy <= margin && interior; ++dy)
                for (int dx = -margin; dx <= margin; ++dx) {
                    const auto neighbour = (static_cast<std::size_t>(y + dy) * image.width + x + dx) * 3;
                    if (baseline.rgb[neighbour] != value) {
                        interior = false;
                        break;
                    }
                }
            if (!interior) continue;
            require(std::abs(int(image.rgb[at]) - int(value)) <= 2,
                    mode + " changed a flat triangle/background interior");
            if (value == 255) ++whiteInterior;
            if (value == 0) ++blackInterior;
        }
    }
    require(whiteInterior > 100 && blackInterior > 100, "fixture has insufficient flat interior pixels");
    std::cout << "AA metrics " << mode << ": " << intermediate << " intermediate pixels\n";
}

aeg::ModelData triangle() {
    aeg::ModelData model;
    model.name = "Sloped AA regression triangle";
    aeg::MeshData mesh;
    mesh.vertices = {{{-0.81f, -0.73f, 0.5f}, {0, 0, 1}, {}},
                     {{0.77f, -0.41f, 0.5f}, {0, 0, 1}, {}},
                     {{-0.43f, 0.83f, 0.5f}, {0, 0, 1}, {}}};
    mesh.indices = {0, 1, 2};
    model.meshes.push_back(std::move(mesh));
    model.materials.emplace_back();
    return model;
}

std::uint32_t expectedSamples(std::uint32_t mask, std::uint32_t requested) {
    for (const auto supported : {8u, 4u, 2u, 1u})
        if (supported <= requested && (mask & supported)) return supported;
    throw std::runtime_error("renderer does not support single-sample rendering");
}

std::string modeName(const aeg::AntialiasingSettings& aa) {
    return std::to_string(aa.msaaSamples) + "x" + (aa.fxaa ? "-fxaa" : "-plain");
}
}

int main(int argc, char** argv) {
    if (argc != 3 || !glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(800, 600, "AEG antialiasing test", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    std::ostringstream diagnostics;
    auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
    int result = 0;
    try {
        const std::filesystem::path runtime(argv[1]), output(argv[2]);
        std::filesystem::create_directories(output);
        aeg::VulkanRenderer renderer(window, runtime / "shaders", true);
        const auto mask = renderer.supportedMsaaSamples();
        require((mask & 1u) && !(mask & ~15u), "invalid supported MSAA mask");
        require(renderer.antialiasing() == aeg::AntialiasingSettings{}, "AA must default to Off");
        aeg::RenderSettings settings;
        settings.unlit = true;
        settings.background = {0.125f, 0.25f, 0.5f, 1};
        constexpr std::uint32_t width = 321, height = 239;

        auto render = [&](std::uint32_t w, std::uint32_t h, aeg::AntialiasingSettings aa,
                          bool setDuringUi = false) {
            if (!setDuringUi) renderer.setAntialiasing(aa);
            bool drew = false;
            for (int retry = 0; retry < 8 && !drew; ++retry) {
                glfwPollEvents();
                renderer.drawFrame(settings, [&] {
                    renderer.resizeViewport(w, h);
                    if (setDuringUi) renderer.setAntialiasing(aa);
                    ImGui::SetNextWindowPos({20, 30}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize({510, 410}, ImGuiCond_Always);
                    ImGui::Begin("AA scaled viewport");
                    // Both target changes precede recording this descriptor in ImGui.
                    ImGui::Image(renderer.viewportTexture(), {457, 337});
                    ImGui::End();
                    drew = true;
                });
            }
            require(drew, "no frame rendered after window/viewport resize");
            require(renderer.antialiasing() == aeg::AntialiasingSettings{expectedSamples(mask, aa.msaaSamples), aa.fxaa},
                    "effective MSAA/FXAA setting or unsupported-count fallback is wrong");
        };
        auto capture = [&](const std::string& name, std::uint32_t w = 321, std::uint32_t h = 239) {
            const auto path = output / (name + ".ppm");
            renderer.captureViewport(path);
            auto image = readPpm(path);
            require(image.width == w && image.height == h, "capture has stale dimensions");
            return image;
        };
        auto rejectStaleCapture = [&] {
            bool rejected = false;
            try {
                renderer.captureViewport(output / "stale.ppm");
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "capture accepted target pixels invalidated by an AA/extent change");
        };

        // Empty models need no upload: every AA combination must preserve constants.
        render(width, height, {});
        const auto flat = capture("empty-off");
        require(flat.rgb[0] > 0 && flat.rgb[0] < flat.rgb[1] && flat.rgb[1] < flat.rgb[2],
                "empty scene did not use the configured nonblack background");
        for (std::size_t i = 0; i < flat.rgb.size(); ++i)
            require(flat.rgb[i] == flat.rgb[i % 3], "empty scene is not spatially constant");
        for (const auto samples : {1u, 2u, 4u, 8u})
            for (const bool fxaa : {false, true}) {
                render(width, height, {samples, fxaa}, true);
                stableFlatImage(capture("empty-" + modeName({samples, fxaa})), flat);
            }

        settings.background = {0, 0, 0, 1};
        const auto source = triangle();
        renderer.uploadModel(source);
        render(width, height, {});
        const auto baseline = capture("triangle-off");
        binaryTriangle(baseline);
        Image fxaaImage;
        for (const auto samples : {1u, 2u, 4u, 8u}) {
            Image withoutFxaa;
            for (const bool fxaa : {false, true}) {
                const aeg::AntialiasingSettings aa{samples, fxaa};
                render(width, height, aa, true);
                const auto image = capture("triangle-" + modeName(aa));
                if (expectedSamples(mask, samples) > 1 || fxaa)
                    antialiasedTriangle(image, baseline, modeName(aa));
                else
                    require(image.rgb == baseline.rgb, "unsupported sample count fallback changed Off pixels");
                if (!fxaa) withoutFxaa = image;
                if (fxaa && expectedSamples(mask, samples) > 1)
                    require(image.rgb != withoutFxaa.rgb, "combined capture omitted the FXAA result");
                if (samples == 1 && fxaa) fxaaImage = image;
            }
        }
        if (!(mask & 14u)) std::cout << "MSAA unavailable; verified advertised 1x fallback\n";

        // Invalid input preserves both the effective mode and its final output.
        render(width, height, {1, true});
        for (const auto invalid : {0u, 3u, 16u}) {
            bool rejected = false;
            try {
                renderer.setAntialiasing({invalid, false});
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected && renderer.antialiasing() == aeg::AntialiasingSettings{1, true},
                    "invalid MSAA request was accepted or changed the effective settings");
            require(capture("invalid-" + std::to_string(invalid)).rgb == fxaaImage.rgb,
                    "invalid MSAA request changed or invalidated the last rendered scene");
        }
        renderer.setAntialiasing({1, false});
        rejectStaleCapture();
        render(width, height, {});
        require(capture("returned-off").rgb == baseline.rgb, "returning to Off did not reproduce baseline pixels");
        renderer.resizeViewport(93, 71);
        rejectStaleCapture();

        // Repeat target replacement with FXAA on/off, every supported count,
        // scaled ImGui sampling, one-pixel axes, odd extents and swapchain resize.
        constexpr std::array<std::array<std::uint32_t, 2>, 6> sizes{{
            {93, 71}, {1, 1}, {1, 37}, {41, 1}, {3, 5}, {319, 181}}};
        std::size_t iteration = 0;
        for (int cycle = 0; cycle < 2; ++cycle)
            for (const auto samples : {1u, 2u, 4u, 8u}) {
                if (!(mask & samples)) continue;
                for (const bool fxaa : {false, true}) {
                    const auto size = sizes[iteration % sizes.size()];
                    glfwSetWindowSize(window, 350 + int(iteration % 3) * 113, 270 + int(iteration % 4) * 59);
                    render(size[0], size[1], {samples, fxaa}, iteration % 2 == 0);
                    const auto image = capture("resized-" + std::to_string(iteration), size[0], size[1]);
                    intermediatePixels(image); // Also catches unexpected tint or corrupt output at tiny sizes.
                    ++iteration;
                }
            }

        // Replace a model while the combined path owns live viewport descriptors.
        auto invisible = source;
        for (auto& vertex : invisible.meshes[0].vertices) vertex.position.x += 4;
        renderer.uploadModel(invisible);
        render(width, height, {8, true});
        const auto emptyReplacement = capture("replaced-offscreen");
        require(std::all_of(emptyReplacement.rgb.begin(), emptyReplacement.rgb.end(), [](auto value) { return value == 0; }),
                "replaced model left stale pixels in the postprocessed output");
        renderer.uploadModel(source);
        render(width, height, {});
        require(capture("restored-off").rgb == baseline.rgb, "resize/model/AA round trip changed Off pixels");
        renderer.waitIdle();
        require(renderer.validationErrorCount() == 0, "Vulkan antialiasing lifecycle validation failed");
    } catch (const std::exception& error) {
        std::cout << "FAIL antialiasing: " << error.what() << '\n';
        result = 1;
    }
    // The renderer is already destroyed, so this includes teardown diagnostics.
    std::cerr.rdbuf(previous);
    if (diagnostics.str().find("[Vulkan error]") != std::string::npos) {
        std::cerr << diagnostics.str();
        result = 1;
    }
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    if (!result)
        std::cout << "PASS AA final-image coverage, flat interiors, fallback, invalid input, model replacement, "
                     "tiny/odd/window resize and clean teardown\n";
    return result;
}
