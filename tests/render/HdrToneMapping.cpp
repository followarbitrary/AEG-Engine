#include <Renderer.hpp>
#include <VulkanBackend.hpp>
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace aeg;
using Color = std::array<double, 3>;
constexpr unsigned extent = 65;
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
void close(double actual, double expected, double tolerance, const std::string& label) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
            label + ": expected=" + std::to_string(expected) + " actual=" + std::to_string(actual));
}

// Forward actual Vulkan resources unchanged. Observing framebuffer construction
// allows raw scene/history readback without exposing a renderer debug API.
class ObservedDevice final : public rhi::Device {
public:
    explicit ObservedDevice(rhi::Device& target) : target_(target) {}
    rhi::Texture* scene{};
    rhi::Texture* fxaaInput{};
    std::vector<rhi::Texture*> history;
    unsigned textureCreations{};
    const rhi::DeviceCapabilities& capabilities() const override { return target_.capabilities(); }
    rhi::TextureSupport textureSupport(rhi::Format f, rhi::TextureUsage u, rhi::TextureDimension d) const override {
        return target_.textureSupport(f, u, d);
    }
    std::unique_ptr<rhi::Buffer> createBuffer(const rhi::BufferDesc& d) override { return target_.createBuffer(d); }
    std::unique_ptr<rhi::Buffer> uploadBuffer(std::span<const std::byte> b, rhi::BufferUsage u) override {
        return target_.uploadBuffer(b, u);
    }
    std::unique_ptr<rhi::Texture> createTexture(const rhi::TextureDesc& d) override {
        ++textureCreations;
        return target_.createTexture(d);
    }
    std::unique_ptr<rhi::Texture> uploadTexture(const rhi::TextureDesc& d, std::span<const std::byte> p) override {
        return target_.uploadTexture(d, p);
    }
    std::unique_ptr<rhi::Sampler> createSampler(const rhi::SamplerDesc& d) override { return target_.createSampler(d); }
    std::unique_ptr<rhi::Shader> createShader(const rhi::ShaderDesc& d) override { return target_.createShader(d); }
    std::unique_ptr<rhi::BindingLayout> createBindingLayout(const rhi::BindingLayoutDesc& d) override {
        return target_.createBindingLayout(d);
    }
    std::unique_ptr<rhi::BindingSet> createBindingSet(rhi::BindingLayout& l,
                                                      std::span<const rhi::BindingWrite> w) override {
        if (w.size() == 1 && w[0].texture && w[0].texture->description().extent == rhi::Extent2D{extent, extent} &&
            w[0].texture->description().format == rhi::Format::Rgba8Unorm)
            fxaaInput = w[0].texture;
        return target_.createBindingSet(l, w);
    }
    std::unique_ptr<rhi::PipelineLayout> createPipelineLayout(const rhi::PipelineLayoutDesc& d) override {
        return target_.createPipelineLayout(d);
    }
    std::unique_ptr<rhi::RenderPass> createRenderPass(const rhi::RenderPassDesc& d) override {
        return target_.createRenderPass(d);
    }
    std::unique_ptr<rhi::Framebuffer> createFramebuffer(const rhi::FramebufferDesc& d) override {
        auto result = target_.createFramebuffer(d);
        if (d.extent == rhi::Extent2D{extent, extent} && d.attachments.size() >= 2) {
            if (rhi::isDepthFormat(d.attachments[1]->description().format)) {
                scene = d.attachments[d.attachments[0]->description().samples > 1 ? 2 : 0];
                history.clear();
                fxaaInput = nullptr;
            } else if (d.attachments[1]->description().format == rhi::Format::R32Float) {
                history.push_back(d.attachments[0]);
            }
        }
        return result;
    }
    std::unique_ptr<rhi::GraphicsPipeline> createGraphicsPipeline(const rhi::GraphicsPipelineDesc& d) override {
        return target_.createGraphicsPipeline(d);
    }
    std::unique_ptr<rhi::ComputePipeline> createComputePipeline(const rhi::ComputePipelineDesc& d) override {
        return target_.createComputePipeline(d);
    }
    void submitImmediate(const std::function<void(rhi::CommandList&)>& f) override { target_.submitImmediate(f); }
    rhi::TextureReadback readback(rhi::Texture& t, rhi::ResourceState s, rhi::TextureSubresource r) override {
        return target_.readback(t, s, r);
    }
    void waitIdle() override { target_.waitIdle(); }
    const std::string& deviceName() const override { return target_.deviceName(); }
    std::uint32_t validationErrorCount() const override { return target_.validationErrorCount(); }

private:
    rhi::Device& target_;
};

double half(std::uint16_t bits) {
    const unsigned exponent = (bits >> 10) & 31, fraction = bits & 1023;
    const double value =
        exponent == 31 ? (fraction ? std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::infinity())
                       : std::ldexp(exponent ? 1.0 + double(fraction) / 1024 : double(fraction) / 1024,
                                    exponent ? int(exponent) - 15 : -14);
    return bits & 0x8000 ? -value : value;
}
std::vector<double> raw(rhi::Device& device, rhi::Texture* texture) {
    require(texture != nullptr, "No observed HDR attachment");
    const auto data = device.readback(*texture);
    require(data.extent == rhi::Extent2D{extent, extent} &&
                (data.format == rhi::Format::Rgba16Float || data.format == rhi::Format::Rgba32Float),
            "Scene/history must be floating-point HDR");
    const bool halfFloat = data.format == rhi::Format::Rgba16Float;
    require(data.pixels.size() == extent * extent * 4 * (halfFloat ? 2u : 4u), "Incomplete HDR readback");
    std::vector<double> values(extent * extent * 4);
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (halfFloat) {
            std::uint16_t bits;
            std::memcpy(&bits, data.pixels.data() + i * 2, 2);
            values[i] = half(bits);
        } else {
            float value;
            std::memcpy(&value, data.pixels.data() + i * 4, 4);
            values[i] = value;
        }
    }
    return values;
}
constexpr std::size_t center = (extent / 2 * extent + extent / 2) * 4;
int quantized(double linear) { return int(std::lround(std::clamp(linear, 0.0, 1.0) * 255)); }
int encoded(double linear) {
    // The public output is linear UNORM8. Its quantization precedes capture's
    // single sRGB transfer, including the intentionally coarse near-black steps.
    const double value = double(quantized(linear)) / 255;
    return int(std::lround(255 * (value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055)));
}
// Neutral input reduces the Hill matrix fit to this scalar rational curve.
// Colored checks below use independently supplied literal double goldens.
double neutral(double value) {
    return std::clamp((value * (value + 0.0245786) - 0.000090537) / (value * (0.983729 * value + 0.432951) + 0.238081),
                      0.0, 1.0);
}
// Independent reference for the AgX fitted mapper. Constants are copied from
// Filament's Apache-2.0 AgxToneMapper (which in turn cites Blender's AgX LUT
// generation), with matrix orientation made explicit for row-vector math.
Color agxReference(Color color) {
    constexpr double srgbToRec2020[3][3] = {
        {0.6274, 0.3293, 0.0433}, {0.0691, 0.9195, 0.0114}, {0.0164, 0.0880, 0.8956}};
    constexpr double rec2020ToSrgb[3][3] = {
        {1.6605, -0.5876, -0.0728}, {-0.1246, 1.1329, -0.0083}, {-0.0182, -0.1006, 1.1187}};
    constexpr double input[3][3] = {{0.856627153315983, 0.0951212405381588, 0.0482516061458583},
                                    {0.137318972929847, 0.761241990602591, 0.101439036467562},
                                    {0.11189821299995, 0.0767994186031903, 0.811302368396859}};
    constexpr double outputMatrix[3][3] = {{1.127100581814436, -0.110606643096603, -0.016493938338833},
                                           {-0.141329763498438, 1.157823702216272, -0.016493938338833},
                                           {-0.141329763498438, -0.110606643096603, 1.25193640659504}};
    constexpr double minEv = -12.47393;
    constexpr double maxEv = 4.026069;
    constexpr double invRange = 1.0 / (maxEv - minEv);
    auto multiply = [](const double matrix[3][3], Color value) {
        return Color{matrix[0][0] * value[0] + matrix[0][1] * value[1] + matrix[0][2] * value[2],
                     matrix[1][0] * value[0] + matrix[1][1] * value[1] + matrix[1][2] * value[2],
                     matrix[2][0] * value[0] + matrix[2][1] * value[1] + matrix[2][2] * value[2]};
    };
    color = {std::max(0.0, color[0]), std::max(0.0, color[1]), std::max(0.0, color[2])};
    color = multiply(srgbToRec2020, color);
    color = multiply(input, color);
    for (auto& channel : color)
        channel = std::clamp((std::log2(std::max(channel, 1e-10)) - minEv) * invRange, 0.0, 1.0);
    for (auto& channel : color) {
        const double x2 = channel * channel, x4 = x2 * x2, x6 = x4 * x2;
        channel = -17.86 * x6 * channel + 78.01 * x6 - 126.7 * x4 * channel + 92.06 * x4 - 28.72 * x2 * channel +
                  4.361 * x2 - 0.1718 * channel + 0.002857;
    }
    color = multiply(outputMatrix, color);
    for (auto& channel : color) channel = std::pow(std::max(channel, 0.0), 2.2);
    color = multiply(rec2020ToSrgb, color);
    for (auto& channel : color) channel = std::clamp(channel, 0.0, 1.0);
    return color;
}
std::vector<std::uint8_t> output(rhi::Device& device, render::Renderer& renderer) {
    const auto texture = renderer.outputTexture();
    require(bool(texture), "Submitted renderer has no display output");
    const auto data = device.readback(*texture);
    require(data.format == rhi::Format::Rgba8Unorm && data.extent == rhi::Extent2D{extent, extent} &&
                data.pixels.size() == extent * extent * 4,
            "Final output must be display-linear RGBA8");
    std::vector<std::uint8_t> result(data.pixels.size());
    std::memcpy(result.data(), data.pixels.data(), result.size());
    for (std::size_t i = 3; i < result.size(); i += 4) require(result[i] == 255, "Tone output alpha must be one");
    return result;
}
void pixel(const std::vector<std::uint8_t>& image, Color expected, const std::string& label, int tolerance = 1) {
    for (unsigned c = 0; c < 3; ++c) close(image[center + c], quantized(expected[c]), tolerance, label);
}
std::vector<std::uint8_t> capture(render::Renderer& renderer, const std::filesystem::path& path) {
    renderer.captureViewport(path);
    std::ifstream file(path, std::ios::binary);
    std::string magic;
    unsigned width{}, height{}, maximum{};
    file >> magic >> width >> height >> maximum;
    require(magic == "P6" && width == extent && height == extent && maximum == 255 && file.get() == '\n',
            "Invalid HDR capture header");
    std::vector<std::uint8_t> result(extent * extent * 3);
    file.read(reinterpret_cast<char*>(result.data()), std::streamsize(result.size()));
    require(file.gcount() == std::streamsize(result.size()) && file.peek() == std::char_traits<char>::eof(),
            "Invalid HDR capture footprint");
    return result;
}
assets::ModelData plane(Color color, float alpha = 1, bool triangle = false) {
    assets::ModelData result;
    result.name = "HDR radiance fixture";
    auto& material = result.materials.emplace_back();
    material.baseColor = {float(color[0]), float(color[1]), float(color[2]), alpha};
    material.alphaMode = alpha < 1 ? assets::AlphaMode::Blend : assets::AlphaMode::Opaque;
    auto& mesh = result.meshes.emplace_back();
    mesh.vertices = {{{-2, -2, -3}, {0, 0, 1}, {}},
                     {{2, -2, -3}, {0, 0, 1}, {}},
                     {{2, 2, -3}, {0, 0, 1}, {}},
                     {{-2, 2, -3}, {0, 0, 1}, {}}};
    if (triangle) {
        // Avoid the viewport's exact 45-degree diagonal: symmetric two-sample
        // positions can both lie on that edge and produce no partial coverage.
        mesh.vertices[0].position = {-1.61f, -1.47f, -3};
        mesh.vertices[1].position = {1.57f, -0.83f, -3};
        mesh.vertices[3].position = {-0.87f, 1.67f, -3};
    }
    mesh.indices = triangle ? std::vector<std::uint32_t>{0, 1, 3} : std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3};
    return result;
}
assets::HdrImageData environment(float value) {
    assets::HdrImageData result{32, 16, std::vector<float>(32 * 16 * 4, value)};
    for (std::size_t i = 3; i < result.rgba.size(); i += 4) result.rgba[i] = 1;
    return result;
}
void camera(render::RenderSettings& settings, bool perspective = false,
            math::ClipSpaceConvention clip = {true, false}) {
    settings.set_camera({math::identity(),
                         perspective ? math::perspective(math::to_radians(60), 1, 0.1f, 20, clip)
                                     : math::orthographic(-2, 2, -2, 2, 0.1f, 20, clip),
                         clip});
}
void suite(rhi::Device& realDevice, const std::filesystem::path& directory) {
    ObservedDevice device(realDevice);
    render::Renderer renderer(device);
    require(renderer.toneMapping() == render::ToneMappingSettings{render::ToneMapper::Aces, 0},
            "Fresh Renderer must default to fitted ACES at EV zero");
    renderer.resizeViewport(extent, extent);
    render::RenderSettings settings;
    settings.lightCount = 0;
    settings.ambient = 0;
    camera(settings);
    auto frame = [&] {
        realDevice.submitImmediate([&](rhi::CommandList& commands) { renderer.recordFrame(commands, settings); });
        renderer.frameSubmitted();
    };
    auto settle = [&](unsigned count = 2) {
        for (unsigned i = 0; i < count; ++i) frame();
    };
    auto clear = [&](Color color, float ev = 0) {
        renderer.setToneMapping({render::ToneMapper::Aces, ev});
        settings.background = {float(color[0]), float(color[1]), float(color[2]), 1};
        frame();
        return output(realDevice, renderer);
    };
    auto agxClear = [&](Color color, float ev = 0) {
        renderer.setToneMapping({render::ToneMapper::Agx, ev});
        settings.background = {float(color[0]), float(color[1]), float(color[2]), 1};
        frame();
        return output(realDevice, renderer);
    };
    constexpr std::array<double, 6> inputs{0.18, 0.5, 1, 2, 4, 16};
    constexpr std::array<double, 6> goldens{0.10559124722893, 0.37430831401646, 0.61911542694081,
                                            0.80356178264339, 0.90901376782661, 0.98993530991463};
    for (unsigned i = 0; i < inputs.size(); ++i) {
        const auto value = inputs[i], golden = goldens[i];
        pixel(clear({value, value, value}), {golden, golden, golden}, "Literal neutral ACES golden");
        const auto image = capture(renderer, directory / ("neutral-" + std::to_string(i) + ".ppm"));
        for (unsigned c = 0; c < 3; ++c)
            close(image[center / 4 * 3 + c], encoded(golden), 1, "Capture applies sRGB exactly once after UNORM8");
        const auto scene = raw(realDevice, device.scene);
        close(scene[center], value, 0.001, "Raw scene retains neutral radiance above one");
    }
    constexpr std::array<Color, 5> coloredInputs{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {4, 1, 0.25}, {16, 0, 0}}};
    constexpr std::array<Color, 5> coloredGoldens{{{0.68802787434169, 0, 0.0026390067466771},
                                                   {0.10161327746151, 0.62365897949847, 0.028843718537714},
                                                   {0, 0, 0.60175884648745},
                                                   {0.99489108424988, 0.66338062024673, 0.31668454820895},
                                                   {1, 0.65197941191945, 0.31432379074825}}};
    for (unsigned i = 0; i < coloredInputs.size(); ++i)
        pixel(clear(coloredInputs[i]), coloredGoldens[i], "Literal colored Hill matrix-fit golden");
    require(clear({1, 1, 1}, 1) == clear({2, 2, 2}), "EV +1 must double scene radiance once");
    require(clear({4, 4, 4}, -2) == clear({1, 1, 1}), "EV -2 must recover unclipped radiance four");

    // AgX fitted neutral and chromatic output is independently pinned here.
    constexpr std::array<double, 6> agxInputs{0.18, 0.5, 1, 2, 4, 16};
    constexpr std::array<double, 6> agxGoldens{0.21484819280153, 0.42244778906279, 0.58292112391332,
                                               0.73216256261178, 0.84904836619458, 0.96036377253473};
    for (unsigned i = 0; i < agxInputs.size(); ++i)
        pixel(agxClear({agxInputs[i], agxInputs[i], agxInputs[i]}), {agxGoldens[i], agxGoldens[i], agxGoldens[i]},
              "Literal neutral AgX fitted golden");
    constexpr std::array<Color, 5> agxColoredInputs{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {4, 1, 0.25}, {16, 0, 0}}};
    constexpr std::array<Color, 5> agxColoredGoldens{{{0.71645869275795, 0.10705653463029, 0.06705302074050},
                                                      {0.23690234612395, 0.57775901578949, 0.13189546430593},
                                                      {0.03442223768487, 0.13600725852325, 0.72166951241225},
                                                      {0.92164434398229, 0.61503693507495, 0.43427539088223},
                                                      {1.00000000000000, 0.70606980380190, 0.60018014450228}}};
    for (unsigned i = 0; i < agxColoredInputs.size(); ++i)
        pixel(agxClear(agxColoredInputs[i]), agxColoredGoldens[i], "Literal colored AgX fitted golden");
    require(agxClear({1, 1, 1}, 1) == agxClear({2, 2, 2}), "AgX EV +1 must double scene radiance once");
    require(agxClear({4, 4, 4}, -2) == agxClear({1, 1, 1}), "AgX EV -2 must recover unclipped radiance four");
    int previous = -1;
    for (double value : {1.0, 2.0, 4.0, 16.0}) {
        const auto image = clear({value, value, value}, -4);
        require(int(image[center]) > previous, "Negative exposure must distinguish increasing HDR values above one");
        previous = image[center];
    }
    renderer.setToneMapping({render::ToneMapper::Linear, 0});
    for (const auto value : {0.0f, 0.18f, 1.0f, 4.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::max()}) {
        settings.background = {value, value, value, 1};
        frame();
        const double sanitized = std::isnan(value) || value < 0 ? 0 : std::min(double(value), 65504.0);
        pixel(output(realDevice, renderer), {sanitized, sanitized, sanitized}, "Linear clamp / clear sanitation", 0);
        const auto scene = raw(realDevice, device.scene);
        for (unsigned c = 0; c < 3; ++c)
            close(scene[center + c], sanitized, sanitized < 1 ? 0.001 : 0, "CPU clear sanitizes before HDR storage");
    }
    renderer.setToneMapping({render::ToneMapper::Aces, 0});
    settings.background = {0, 0, 0, 1};
    settings.unlit = true;
    renderer.uploadModel(plane({4, 1, 0.25}));
    frame();
    pixel(output(realDevice, renderer), coloredGoldens[3], "Unlit output receives ACES");
    renderer.uploadModel(plane({4, 4, 4}, 0.5f));
    frame();
    pixel(output(realDevice, renderer), {goldens[3], goldens[3], goldens[3]}, "HDR alpha composition precedes curve");
    close(raw(realDevice, device.scene)[center], 2, 0.001, "Transparent source blends four to two before tone");

    std::vector<render::AntialiasingSettings> modes{{1, false, false}, {1, true, false}};
    for (unsigned samples : {2u, 4u, 8u})
        if (renderer.supportedMsaaSamples() & samples) {
            modes.push_back({samples, false, false});
            modes.push_back({samples, true, false});
        }
    if (renderer.supportsTemporalAA()) {
        modes.push_back({1, false, true});
        modes.push_back({1, true, true});
    }
    renderer.uploadModel(plane({4, 4, 4}));
    for (const auto aa : modes) {
        renderer.setAntialiasing(aa);
        for (const bool perspective : {false, true})
            for (const bool yDown : {false, true})
                for (const bool reversed : {false, true}) {
                    camera(settings, perspective, {yDown, reversed});
                    settle(aa.taa ? 3 : 1);
                    pixel(output(realDevice, renderer), {goldens[4], goldens[4], goldens[4]},
                          "HDR AA / projection / clip convention");
                    close(raw(realDevice, device.scene)[center], 4, 0.001, "AA scene radiance remains HDR");
                }
    }
    camera(settings);
    // At a sloped triangle edge, compare the independently observed resolved
    // linear coverage against the scalar curve. Tone-before-resolve fails.
    for (unsigned samples : {2u, 4u, 8u}) {
        if (!(renderer.supportedMsaaSamples() & samples)) continue;
        renderer.setAntialiasing({samples, false, false});
        renderer.uploadModel(plane({4, 4, 4}, 1, true));
        frame();
        const auto scene = raw(realDevice, device.scene);
        const auto image = output(realDevice, renderer);
        unsigned partial{};
        for (std::size_t i = 0; i < scene.size(); i += 4) {
            if (scene[i] <= 0.01 || scene[i] >= 3.99) continue;
            ++partial;
            close(image[i], quantized(neutral(scene[i])), 1, "MSAA resolves radiance before curve");
        }
        std::cout << "HDR MSAA " << samples << "x: " << partial << " partially covered pixels\n";
        require(partial > 0, "MSAA HDR fixture has no partially covered edge at " + std::to_string(samples) + "x");
    }
    renderer.setAntialiasing({1, true, false});
    renderer.uploadModel(plane({4, 4, 4}, 1, true));
    frame();
    require(device.fxaaInput != nullptr, "FXAA must sample the display-linear RGBA8 tone result");
    const auto toneInput = realDevice.readback(*device.fxaaInput);
    const auto hdrInput = raw(realDevice, device.scene);
    const auto filtered = output(realDevice, renderer);
    unsigned changed{};
    for (std::size_t i = 0; i < hdrInput.size(); i += 4) {
        const int mapped = std::to_integer<unsigned char>(toneInput.pixels[i]);
        close(mapped, quantized(neutral(hdrInput[i])), 1, "FXAA input has already received the tone curve");
        changed += mapped != filtered[i];
    }
    require(changed > 0, "FXAA did not filter the mapped HDR triangle edge");
    renderer.setToneMapping({render::ToneMapper::Agx, 0});
    renderer.uploadModel(plane({4, 4, 4}, 1, true));
    frame();
    require(device.fxaaInput != nullptr, "AgX FXAA must sample the display-linear tone result");
    const auto agxToneInput = realDevice.readback(*device.fxaaInput);
    const auto agxHdrInput = raw(realDevice, device.scene);
    const auto agxFiltered = output(realDevice, renderer);
    unsigned agxChanged{};
    for (std::size_t i = 0; i < agxHdrInput.size(); i += 4) {
        const int mapped = std::to_integer<unsigned char>(agxToneInput.pixels[i]);
        close(mapped, quantized(agxReference({agxHdrInput[i], agxHdrInput[i + 1], agxHdrInput[i + 2]})[0]), 1,
              "AgX FXAA input has already received the tone curve");
        agxChanged += mapped != agxFiltered[i];
    }
    require(agxChanged > 0, "AgX FXAA did not filter the mapped HDR triangle edge");
    if (renderer.supportsTemporalAA()) {
        renderer.setAntialiasing({1, false, true});
        renderer.uploadModel(plane({4, 4, 4}));
        settle(4);
        require(device.history.size() == 2 && renderer.temporalFrameCount() >= 4, "TAA history did not accumulate");
        const auto resources = device.history;
        const auto creations = device.textureCreations;
        auto count = renderer.temporalFrameCount();
        for (const auto tone : {render::ToneMappingSettings{render::ToneMapper::Aces, -2},
                                render::ToneMappingSettings{render::ToneMapper::Linear, -3},
                                render::ToneMappingSettings{render::ToneMapper::Aces, 1}}) {
            const auto before = output(realDevice, renderer);
            renderer.setToneMapping(tone);
            require(renderer.temporalFrameCount() == count && device.history == resources &&
                        device.textureCreations == creations && output(realDevice, renderer) == before,
                    "Tone settings must preserve history, resources and submitted output");
            frame();
            require(renderer.temporalFrameCount() == ++count, "Tone settings reset temporal accumulation");
            for (auto* history : resources)
                close(raw(realDevice, history)[center], 4, 0.01, "TAA history stores unexposed HDR radiance");
            const double expected =
                tone.mapper == render::ToneMapper::Linear ? 0.5 : neutral(4 * std::exp2(tone.exposureEV));
            pixel(output(realDevice, renderer), {expected, expected, expected}, "Tone follows current HDR history");
        }
        renderer.resetTemporalHistory();
        require(renderer.temporalFrameCount() == 0, "Explicit TAA reset stopped working");
        frame();
        require(renderer.temporalFrameCount() == 1, "Explicitly reset history did not restart");
        const auto submitted = capture(renderer, directory / "submitted.ppm");
        count = renderer.temporalFrameCount();
        renderer.setToneMapping({render::ToneMapper::Linear, -12});
        bool abandoned{};
        try {
            realDevice.submitImmediate([&](rhi::CommandList& commands) {
                renderer.recordFrame(commands, settings);
                throw std::runtime_error("Intentional HDR recording abandonment");
            });
        } catch (const std::runtime_error& error) {
            abandoned = std::string(error.what()) == "Intentional HDR recording abandonment";
        }
        require(abandoned && renderer.temporalFrameCount() == count, "Abandoned recording advanced HDR history");
        require(capture(renderer, directory / "abandoned.ppm") == submitted,
                "Capture after abandoned recording lost the submitted display output");
        frame();
        require(renderer.temporalFrameCount() == count + 1, "Successful frame after abandonment did not advance once");
    }
    renderer.setAntialiasing({});
    renderer.setToneMapping({render::ToneMapper::Aces, 0});
    settings.unlit = false;
    settings.lightCount = 1;
    settings.lights[0].direction = {0, 0, -1};
    settings.lights[0].intensity = 16;
    renderer.uploadModel(plane({0.7, 0.3, 0.1}));
    frame();
    const auto direct = output(realDevice, renderer);
    const auto directRaw = raw(realDevice, device.scene);
    require(directRaw[center] > 1, "Direct PBR fixture never reached HDR");
    settings.lights[0].intensity = 32;
    renderer.setToneMapping({render::ToneMapper::Aces, -1});
    frame();
    const auto recovered = output(realDevice, renderer);
    for (unsigned c = 0; c < 3; ++c)
        close(recovered[center + c], direct[center + c], 1, "Direct PBR exposure recovery");
    if (renderer.supportsEnvironment()) {
        settings.lightCount = 0;
        renderer.uploadEnvironment(environment(1));
        renderer.setEnvironment({true, 1, 0});
        renderer.setToneMapping({render::ToneMapper::Aces, 0});
        frame();
        const auto one = output(realDevice, renderer);
        renderer.uploadEnvironment(environment(2));
        renderer.setToneMapping({render::ToneMapper::Aces, -1});
        frame();
        const auto two = output(realDevice, renderer);
        for (unsigned c = 0; c < 3; ++c)
            close(two[center + c], one[center + c], 1, "IBL two at EV -1 equals one at EV zero");
        renderer.setEnvironment({false, 1, 0});
    }
    settings.lightCount = render::kMaxLights;
    for (auto& light : settings.lights) {
        light.direction = {0, 0, -1};
        light.color = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max()};
        light.intensity = std::numeric_limits<float>::max();
    }
    auto extreme = plane({1, 1, 1});
    extreme.materials[0].metallicFactor = 1;
    extreme.materials[0].roughnessFactor = 0.045f;
    renderer.uploadModel(extreme);
    if (renderer.supportsTemporalAA()) renderer.setAntialiasing({1, false, true});
    renderer.setToneMapping({render::ToneMapper::Aces, 12});
    settle(2);
    auto bounded = [&](rhi::Texture* texture) {
        const auto values = raw(realDevice, texture);
        for (std::size_t i = 0; i < values.size(); i += 4)
            for (unsigned c = 0; c < 3; ++c)
                require(std::isfinite(values[i + c]) && values[i + c] >= 0 && values[i + c] <= 65504,
                        "Maximum-light PBR wrote NaN/Inf or exceeded the common HDR storage ceiling");
        close(values[center], 65504, 0, "Extreme PBR reaches finite HDR ceiling before storage");
    };
    bounded(device.scene);
    if (renderer.supportsTemporalAA())
        for (auto* history : device.history) bounded(history);
    pixel(output(realDevice, renderer), {1, 1, 1}, "Maximum exposure of finite HDR remains white", 0);
    renderer.setToneMapping({render::ToneMapper::Agx, 12});
    settle(2);
    const auto agxExtreme = output(realDevice, renderer);
    const auto agxCeiling = agxReference({65504, 65504, 65504});
    pixel(agxExtreme, agxCeiling, "AgX maximum exposure of finite HDR remains finite", 0);
    renderer.waitIdle();
    require(renderer.validationErrorCount() == 0, "HDR GPU contracts produced Vulkan validation errors");
}
}

int main(int argc, char** argv) {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(64, 64, "AEG HDR tone mapping contracts", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    std::ostringstream diagnostics;
    auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
    int result{};
    try {
        const auto directory =
            argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path() / "aeg-hdr-tone-mapping";
        std::filesystem::create_directories(directory);
        rhi::NativeWindow native;
        native.window = glfwGetWin32Window(window);
        native.application = GetModuleHandleW(nullptr);
        native.framebufferExtent = [] { return rhi::Extent2D{64, 64}; };
        auto device = rhi::vulkan::createVulkanDevice({native, true});
        suite(*device, directory);
        device->waitIdle();
        require(device->validationErrorCount() == 0, "HDR teardown produced Vulkan validation errors");
        device.reset();
        require(diagnostics.str().find("[Vulkan error]") == std::string::npos,
                "HDR diagnostics contain Vulkan validation errors");
        std::cout
            << "PASS HDR GPU: default/colored ACES, linear SDR capture, raw HDR, exposure, alpha, AA/clip, "
               "resolve ordering, unexposed temporal history, abandoned recording, direct PBR/IBL and finite ceiling; "
               "Vulkan errors: 0\n";
    } catch (const std::exception& error) {
        std::cout << "FAIL HDR GPU contracts: " << error.what() << '\n';
        result = 1;
    }
    std::cerr.rdbuf(previous);
    if (result) std::cerr << diagnostics.str();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
