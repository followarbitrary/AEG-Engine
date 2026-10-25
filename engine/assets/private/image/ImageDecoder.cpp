#include "ImageDecoder.hpp"
#include "ResourcePath.hpp"
#include <Utf8Path.hpp>
#include <assimp/scene.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <algorithm>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace aeg::assets::detail {
namespace {
ImageData decode(const unsigned char* bytes, std::size_t size) {
    if (size == 0 || size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("texture byte size is invalid");
    int width = 0, height = 0, channels = 0;
    using Pixels = std::unique_ptr<unsigned char, decltype(&stbi_image_free)>;
    Pixels pixels(stbi_load_from_memory(bytes, static_cast<int>(size), &width, &height, &channels, 4), stbi_image_free);
    if (!pixels || width <= 0 || height <= 0) throw std::runtime_error("texture is not a supported image");
    ImageData image;
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    image.rgba.assign(pixels.get(), pixels.get() + count);
    return image;
}
}
ImageData readTexture(const aiScene& scene, const aiString& name, const std::filesystem::path& directory, bool gltf) {
    if (const auto* embedded = scene.GetEmbeddedTexture(name.C_Str())) {
        if (embedded->mHeight == 0)
            return decode(reinterpret_cast<const unsigned char*>(embedded->pcData), embedded->mWidth);
        ImageData image;
        image.width = embedded->mWidth;
        image.height = embedded->mHeight;
        const auto count = static_cast<std::size_t>(image.width) * image.height;
        if (!count || !embedded->pcData || count > std::numeric_limits<std::size_t>::max() / 4)
            throw std::runtime_error("embedded texture dimensions are invalid");
        image.rgba.resize(count * 4);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& pixel = embedded->pcData[i];
            image.rgba[i * 4] = pixel.r;
            image.rgba[i * 4 + 1] = pixel.g;
            image.rgba[i * 4 + 2] = pixel.b;
            image.rgba[i * 4 + 3] = pixel.a;
        }
        return image;
    }
    std::string relative = name.C_Str();
    std::replace(relative.begin(), relative.end(), '\\', '/');
    if (gltf) relative = percentDecode(relative);
    const auto path = directory / foundation::pathFromUtf8(relative);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("file could not be opened");
    const auto size = file.tellg();
    if (size <= 0 || size > std::numeric_limits<int>::max()) throw std::runtime_error("texture byte size is invalid");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("texture file is incomplete");
    return decode(bytes.data(), bytes.size());
}
}
