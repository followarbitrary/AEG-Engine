#include <EnvironmentImage.hpp>
#include <stb_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace aeg::assets {
namespace {
// stb's legacy HDR path does not check every short read. Validate the payload
// before decoding so an incomplete image cannot be published as black pixels.
void validatePayload(const std::vector<unsigned char>& bytes, std::uint32_t width, std::uint32_t height) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::size_t offset = 0;
    const auto line = [&]() {
        const auto end = text.find('\n', offset);
        if (end == std::string_view::npos) throw std::runtime_error("HDR header is incomplete");
        auto result = text.substr(offset, end - offset);
        offset = end + 1;
        if (!result.empty() && result.back() == '\r') result.remove_suffix(1);
        return result;
    };
    while (!line().empty()) {
    }
    const auto resolution = line();
    std::istringstream fields{std::string(resolution)};
    std::string y, x;
    std::uint64_t actualHeight{}, actualWidth{};
    if (!(fields >> y >> actualHeight >> x >> actualWidth) || y != "-Y" || x != "+X" || actualHeight != height ||
        actualWidth != width)
        throw std::runtime_error("HDR orientation must be top-first -Y, +X");
    const auto requireBytes = [&](std::size_t count) {
        if (count > bytes.size() - offset) throw std::runtime_error("HDR pixel payload is incomplete");
    };
    if (width < 8) {
        requireBytes(static_cast<std::size_t>(width) * height * 4);
        return;
    }
    for (std::uint32_t row = 0; row < height; ++row) {
        requireBytes(4);
        if (bytes[offset] != 2 || bytes[offset + 1] != 2 || (bytes[offset + 2] & 128)) {
            if (row != 0) throw std::runtime_error("HDR scanline encoding is inconsistent");
            requireBytes(static_cast<std::size_t>(width) * height * 4);
            return;
        }
        if ((static_cast<std::uint32_t>(bytes[offset + 2]) << 8 | bytes[offset + 3]) != width)
            throw std::runtime_error("HDR scanline width is invalid");
        offset += 4;
        for (unsigned channel = 0; channel < 4; ++channel) {
            std::uint32_t decoded = 0;
            while (decoded < width) {
                requireBytes(1);
                const auto code = bytes[offset++];
                const std::uint32_t count = code > 128 ? code - 128u : code;
                if (!count || count > width - decoded) throw std::runtime_error("HDR scanline run is invalid");
                const auto encoded = code > 128 ? 1u : count;
                requireBytes(encoded);
                offset += encoded;
                decoded += count;
            }
        }
    }
}
}

HdrImageData loadHdrImage(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("HDR file could not be opened");
    const auto length = file.tellg();
    constexpr std::streamoff maximumFileBytes = 128 * 1024 * 1024;
    if (length <= 0 || length > maximumFileBytes)
        throw std::runtime_error("HDR file size is invalid (maximum 128 MiB)");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("HDR file read is incomplete");
    if (!stbi_is_hdr_from_memory(bytes.data(), static_cast<int>(bytes.size())))
        throw std::runtime_error("Environment input must be Radiance RGBE HDR");
    int width{}, height{}, channels{};
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels) || width < 2 ||
        height < 1 || width > 4096 || height > 2048 || static_cast<std::int64_t>(width) != 2ll * height)
        throw std::runtime_error("HDR environment must be 2:1 and at most 4096 x 2048");
    validatePayload(bytes, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    using Pixels = std::unique_ptr<float, decltype(&stbi_image_free)>;
    Pixels pixels(stbi_loadf_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4),
                  stbi_image_free);
    if (!pixels) throw std::runtime_error("HDR pixel data could not be decoded");
    HdrImageData image{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), {}};
    const auto count = static_cast<std::size_t>(width) * height * 4;
    image.rgba.assign(pixels.get(), pixels.get() + count);
    for (std::size_t pixel = 0; pixel < count; pixel += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel)
            if (!std::isfinite(image.rgba[pixel + channel]) || image.rgba[pixel + channel] < 0)
                throw std::runtime_error("HDR radiance must be finite and nonnegative");
        image.rgba[pixel + 3] = 1;
    }
    return image;
}

HdrImageData makeProceduralEnvironment() {
    HdrImageData image{512, 256, {}};
    image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4);
    constexpr float pi = std::numbers::pi_v<float>;
    const float sunLength = std::sqrt(.3f * .3f + .7f * .7f + .4f * .4f);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        const float latitude = pi * (static_cast<float>(y) + .5f) / image.height;
        const float up = std::cos(latitude), ring = std::sin(latitude);
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const float longitude = 2 * pi * ((static_cast<float>(x) + .5f) / image.width - .5f);
            const float towardSun =
                (ring * std::cos(longitude) * .3f + up * .7f + ring * std::sin(longitude) * .4f) / sunLength;
            const float sun = 24 * std::pow(std::max(0.0f, towardSun), 512.0f);
            const float sky = std::sqrt(std::max(up, 0.0f)), ground = std::sqrt(std::max(-up, 0.0f));
            const auto pixel = (static_cast<std::size_t>(y) * image.width + x) * 4;
            image.rgba[pixel] = .22f + .12f * sky - .17f * ground + sun;
            image.rgba[pixel + 1] = .26f + .22f * sky - .21f * ground + .85f * sun;
            image.rgba[pixel + 2] = .32f + .42f * sky - .27f * ground + .65f * sun;
            image.rgba[pixel + 3] = 1;
        }
    }
    return image;
}
}
