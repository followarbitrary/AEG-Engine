#include "RendererImpl.hpp"
#include <iostream>

namespace aeg::render {
void Renderer::Impl::captureViewport(const std::filesystem::path& path) {
    if (!submittedOutput) throw std::runtime_error("Cannot capture before the first rendered scene");
    device.waitIdle();
    const auto readback = device.readback(*submittedOutput);
    const auto viewportWidth = viewport->width, viewportHeight = viewport->height;
    if (readback.extent != viewport->extent() || readback.format != displayFormat)
        throw std::runtime_error("Unexpected viewport readback format or extent");
    constexpr auto pixelBytes = 4u;
    const auto bytes = static_cast<std::size_t>(viewportWidth) * viewportHeight * pixelBytes;
    if (readback.pixels.size() != bytes) throw std::runtime_error("Incomplete viewport readback");
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot write scene capture: " + foundation::pathToUtf8(path));
    output << "P6\n" << viewportWidth << ' ' << viewportHeight << "\n255\n";
    const auto* pixels = reinterpret_cast<const unsigned char*>(readback.pixels.data());
    std::vector<unsigned char> rgb(static_cast<std::size_t>(viewportWidth) * viewportHeight * 3);
    for (std::size_t i = 0, j = 0; i < bytes; i += pixelBytes, j += 3)
        for (std::size_t c = 0; c < 3; ++c) {
            // Display-linear RGBA8 quantization precedes the single sRGB encoding.
            const float linear = pixels[i + c] / 255.0f;
            const float srgb = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            rgb[j + c] = static_cast<unsigned char>(std::clamp(srgb * 255.0f, 0.0f, 255.0f) + 0.5f);
        }
    output.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (!output) throw std::runtime_error("Failed writing scene capture");
    std::cout << "Scene capture: " << foundation::pathToUtf8(path) << '\n';
}
}
