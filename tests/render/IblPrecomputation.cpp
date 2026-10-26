#include "environment/EnvironmentBaker.hpp"
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
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace aeg;
using V = std::array<double, 3>;
constexpr double pi = std::numbers::pi;
void require(bool value, const std::string& label) {
    if (!value) throw std::runtime_error(label);
}
void checkClose(double actual, double expected, double tolerance, const std::string& label) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
            label + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}
V direction(unsigned face, unsigned x, unsigned y, unsigned size) {
    const double s = 2 * (double(x) + 0.5) / size - 1, t = 2 * (double(y) + 0.5) / size - 1;
    V v{};
    switch (face) {
    case 0:
        v = {1, -t, -s};
        break;
    case 1:
        v = {-1, -t, s};
        break;
    case 2:
        v = {s, 1, t};
        break;
    case 3:
        v = {s, -1, -t};
        break;
    case 4:
        v = {s, -t, 1};
        break;
    default:
        v = {-s, -t, -1};
        break;
    }
    const double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (auto& component : v) component /= length;
    return v;
}
constexpr V bias{2, 1.5, 1.25}, amplitude{1.2, 0.9, 0.8};
assets::HdrImageData environment(V constant, bool directional = false) {
    assets::HdrImageData image{256, 128, std::vector<float>(256 * 128 * 4)};
    for (unsigned y = 0; y < image.height; ++y)
        for (unsigned x = 0; x < image.width; ++x) {
            const double theta = pi * (double(y) + 0.5) / image.height;
            const double phi = 2 * pi * ((double(x) + 0.5) / image.width - 0.5);
            const V d{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
            const auto offset = (std::size_t(y) * image.width + x) * 4;
            for (unsigned c = 0; c < 3; ++c)
                image.rgba[offset + c] = float(directional ? bias[c] + amplitude[c] * d[c] : constant[c]);
            image.rgba[offset + 3] = 1;
        }
    return image;
}
std::vector<float> read(rhi::Device& device, rhi::Texture& texture, unsigned mip = 0, unsigned face = 0) {
    const auto data = device.readback(texture, rhi::ResourceState::Sampled, {mip, face});
    const unsigned size = std::max(1u, texture.description().extent.width >> mip);
    require(data.format == rhi::Format::Rgba32Float && data.extent == rhi::Extent2D{size, size} &&
                data.pixels.size() == std::size_t(size) * size * 4 * sizeof(float),
            "Unexpected floating-point IBL subresource readback");
    std::vector<float> values(data.pixels.size() / sizeof(float));
    std::memcpy(values.data(), data.pixels.data(), data.pixels.size());
    return values;
}
void cubeContract(const rhi::Texture& texture, unsigned size, unsigned mipCount, const std::string& label) {
    const auto& desc = texture.description();
    require(desc.dimension == rhi::TextureDimension::Cube && desc.extent == rhi::Extent2D{size, size} &&
                desc.mipLevels == mipCount && desc.format == rhi::Format::Rgba32Float && desc.samples == 1 &&
                rhi::contains(desc.usage,
                              rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage | rhi::TextureUsage::CopySource),
            label + " does not expose the contracted HDR cube/mips/usages");
}
void constantCube(rhi::Device& device, rhi::Texture& texture, V value, double tolerance, const std::string& label) {
    for (unsigned mip = 0; mip < texture.description().mipLevels; ++mip)
        for (unsigned face = 0; face < 6; ++face) {
            const auto values = read(device, texture, mip, face);
            for (std::size_t p = 0; p < values.size(); p += 4) {
                const auto where = label + " mip=" + std::to_string(mip) + " face=" + std::to_string(face) +
                                   " pixel=" + std::to_string(p / 4);
                for (unsigned c = 0; c < 3; ++c) checkClose(values[p + c], value[c], tolerance, where);
                checkClose(values[p + 3], 1, 1e-6, where + " alpha");
            }
        }
}

// Independent deterministic LIGHT-hemisphere integration. This does not sample
// GGX half vectors, use Hammersley, or reuse any production BRDF helper. The
// Lambda definition of G also differs algebraically from shader visibility.
std::array<double, 2> dfg(double noV, double roughness, unsigned muSteps = 384) {
    const unsigned phiSteps = muSteps * 2;
    const double vx = std::sqrt(1 - noV * noV), alphaSquared = std::pow(roughness, 4);
    const auto lambda = [alphaSquared](double cosine) {
        return 0.5 * (std::sqrt(1 + alphaSquared * (1 - cosine * cosine) / (cosine * cosine)) - 1);
    };
    std::array<double, 2> result{};
    for (unsigned m = 0; m < muSteps; ++m) {
        const double noL = (double(m) + 0.5) / muSteps;
        const double radial = std::sqrt(1 - noL * noL);
        const double g = 1 / (1 + lambda(noV) + lambda(noL));
        for (unsigned p = 0; p < phiSteps; ++p) {
            const double phi = 2 * pi * (double(p) + 0.5) / phiSteps;
            const double lx = radial * std::cos(phi), ly = radial * std::sin(phi);
            const double hx = vx + lx, hy = ly, hz = noV + noL;
            const double length = std::sqrt(hx * hx + hy * hy + hz * hz);
            const double noH = hz / length, voH = (vx * hx + noV * hz) / length;
            const double denominator = 1 + noH * noH * (alphaSquared - 1);
            const double distribution = alphaSquared / (pi * denominator * denominator);
            const double fc = std::pow(std::clamp(1 - voH, 0.0, 1.0), 5);
            const double weight = distribution * g / (4 * noV);
            result[0] += weight * (1 - fc);
            result[1] += weight * fc;
        }
    }
    const double solidAngle = 2 * pi / (double(muSteps) * phiSteps);
    for (auto& component : result) component *= solidAngle;
    return result;
}
void checkLut(rhi::Device& device, rhi::Texture& lut) {
    const auto& desc = lut.description();
    require(desc.dimension == rhi::TextureDimension::Texture2D && desc.extent == rhi::Extent2D{16, 16} &&
                desc.mipLevels == 1,
            "BRDF LUT dimensions changed");
    const auto values = read(device, lut);
    // Includes both near-grazing columns, minimum/maximum roughness rows and
    // every other texel: no NaN, undefined blue/alpha or negative energy.
    for (std::size_t p = 0; p < values.size(); p += 4) {
        require(std::isfinite(values[p]) && std::isfinite(values[p + 1]) && values[p] >= 0 && values[p + 1] >= 0 &&
                    values[p] <= 1.2f && values[p + 1] <= 1.2f,
                "Nonfinite/out-of-range DFG edge texel");
        checkClose(values[p + 2], 0, 1e-6, "DFG blue");
        checkClose(values[p + 3], 1, 1e-6, "DFG alpha");
    }
    for (const unsigned y : {6u, 10u, 14u})
        for (const unsigned x : {5u, 10u, 14u}) {
            const double noV = (double(x) + 0.5) / 16;
            const double roughness = 0.045 + 0.955 * (double(y) + 0.5) / 16;
            const auto expected = dfg(noV, roughness), coarse = dfg(noV, roughness, 192);
            for (unsigned c = 0; c < 2; ++c) {
                checkClose(coarse[c], expected[c], 0.001, "Independent DFG quadrature convergence");
                checkClose(values[(y * 16 + x) * 4 + c], expected[c], c == 0 ? 0.022 : 0.004,
                           "DFG hemisphere integral x=" + std::to_string(x) + " y=" + std::to_string(y) +
                               " component=" + std::to_string(c));
            }
        }
}
void directional(rhi::Device& device, render::EnvironmentBaker& baker) {
    auto maps = baker.bake(environment({}, true));
    for (unsigned face = 0; face < 6; ++face) {
        const auto source = read(device, *maps.environment, 0, face);
        // Every pixel includes all four cube edges and corners. Comparing the
        // continuous analytic radiance on both sides detects seam/clamp errors
        // as well as face order, handedness, V orientation and pole inversion.
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned x = 0; x < 16; ++x) {
                const auto d = direction(face, x, y, 16);
                for (unsigned c = 0; c < 3; ++c)
                    checkClose(source[(y * 16 + x) * 4 + c], bias[c] + amplitude[c] * d[c], 0.014,
                               "Directional equirectangular cube orientation/seam face=" + std::to_string(face));
            }
        const auto irradiance = read(device, *maps.irradiance, 0, face);
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x) {
                const auto d = direction(face, x, y, 8);
                for (unsigned c = 0; c < 3; ++c)
                    checkClose(irradiance[(y * 8 + x) * 4 + c], pi * bias[c] + 2 * pi / 3 * amplitude[c] * d[c], 0.065,
                               "Analytic first-order spherical irradiance");
            }
    }
    // Odd radiance survives a sharp convolution but loses directional
    // contrast at roughness 1. Comparing opposing faces removes the DC term.
    const auto sharpPositive = read(device, *maps.prefiltered, 0, 0);
    const auto sharpNegative = read(device, *maps.prefiltered, 0, 1);
    const auto roughPositive = read(device, *maps.prefiltered, maps.prefilterMipCount - 1, 0);
    const auto roughNegative = read(device, *maps.prefiltered, maps.prefilterMipCount - 1, 1);
    const auto center = (4 * 8 + 4) * 4;
    const double sharpContrast = sharpPositive[center] - sharpNegative[center];
    const double roughContrast = roughPositive[0] - roughNegative[0];
    require(sharpContrast > 2.1 && roughContrast > 0.5 && roughContrast < sharpContrast * 0.85,
            "GGX prefilter roughness failed to reduce directional contrast: sharp=" + std::to_string(sharpContrast) +
                ", rough=" + std::to_string(roughContrast));
    for (unsigned mip = 0; mip < maps.prefilterMipCount; ++mip)
        for (unsigned face = 0; face < 6; ++face)
            for (const float value : read(device, *maps.prefiltered, mip, face))
                require(std::isfinite(value) && value >= 0, "Directional prefilter contains invalid radiance");
}
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(64, 64, "AEG independent compute IBL test", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    std::ostringstream diagnostics;
    auto* previous = std::cerr.rdbuf(diagnostics.rdbuf());
    int result{};
    try {
        rhi::NativeWindow native;
        native.window = glfwGetWin32Window(window);
        native.application = GetModuleHandleW(nullptr);
        native.framebufferExtent = [] { return rhi::Extent2D{64, 64}; };
        auto device = rhi::vulkan::createVulkanDevice({native, true});
        require(render::EnvironmentBaker::supported(*device), "Test GPU does not support compute IBL");
        {
            render::EnvironmentBaker baker(*device, {16, 8, 8, 16, 512});
            auto* immutableLut = &baker.brdfLut();
            checkLut(*device, *immutableLut);
            {
                // Values deliberately exceed 1: sRGB decoding, UNORM storage,
                // clipping or inadvertent tone mapping cannot pass this test.
                const V hdr{2, 3.5, 5.25};
                auto maps = baker.bake(environment(hdr));
                require(maps.environment && maps.irradiance && maps.prefiltered && maps.prefilterMipCount == 4,
                        "Baker returned incomplete environment products");
                cubeContract(*maps.environment, 16, 5, "Environment");
                cubeContract(*maps.irradiance, 8, 1, "Irradiance");
                cubeContract(*maps.prefiltered, 8, 4, "Prefilter");
                constantCube(*device, *maps.environment, hdr, 0.003, "HDR source preservation");
                constantCube(*device, *maps.prefiltered, hdr, 0.004, "HDR GGX preservation");
                constantCube(*device, *maps.irradiance, {pi * hdr[0], pi * hdr[1], pi * hdr[2]}, 0.006,
                             "Actual hemispherical irradiance E=pi*Li");
            }
            {
                auto maps = baker.bake(environment({0, 0, 0}));
                constantCube(*device, *maps.environment, {}, 1e-7, "Black environment");
                constantCube(*device, *maps.irradiance, {}, 1e-7, "Black irradiance");
                constantCube(*device, *maps.prefiltered, {}, 1e-7, "Black GGX including minimum roughness endpoint");
            }
            directional(*device, baker);
            require(&baker.brdfLut() == immutableLut, "Environment replacement recreated the immutable BRDF LUT");
        }
        device->waitIdle();
        require(device->validationErrorCount() == 0, "Vulkan validation rejected compute IBL");
        require(diagnostics.str().find("Vulkan validation error") == std::string::npos,
                "Compute IBL diagnostics contain a Vulkan validation error");
        std::cout << "Compute IBL: all six faces/all mips, HDR/black preservation, pi irradiance, independent DFG "
                     "quadrature, orientation/seams and roughness blur; Vulkan errors: 0\n";
    } catch (const std::exception& error) {
        std::cout << "Compute IBL test failed: " << error.what() << '\n';
        result = 1;
    }
    std::cerr.rdbuf(previous);
    if (result) std::cerr << diagnostics.str();
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
