#include <aeg/Model.hpp>
#include <assimp/Importer.hpp>
#include <assimp/GltfMaterial.h>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace aeg {
namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto s = path.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
std::filesystem::path pathFromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
std::string lowerExtension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}
bool isGltf(const std::filesystem::path& path) {
    const auto extension = lowerExtension(path);
    return extension == ".gltf" || extension == ".glb";
}
bool hasPmxSignature(const std::filesystem::path& path) {
    std::array<char, 4> signature{};
    std::ifstream input(path, std::ios::binary);
    return input.read(signature.data(), signature.size()) &&
           signature == std::array<char, 4>{'P', 'M', 'X', ' '};
}
void validatePmxHeader(const std::filesystem::path& path) {
    // Assimp's PMX reader uses these sizes before it can validate a scene.
    // Check the fixed header first; conversion references are checked by our
    // tracked MMD importer patch. This is not a replacement PMX parser.
    std::array<unsigned char, 17> header{};
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(header.data()), header.size()))
        throw std::runtime_error("PMX header is truncated");
    if (header[0] != 'P' || header[1] != 'M' || header[2] != 'X' || header[3] != ' ')
        throw std::runtime_error("Invalid PMX signature");
    const auto versionBits = std::uint32_t(header[4]) | (std::uint32_t(header[5]) << 8) |
                             (std::uint32_t(header[6]) << 16) | (std::uint32_t(header[7]) << 24);
    const float version = std::bit_cast<float>(versionBits);
    if (version != 2.0f && version != 2.1f)
        throw std::runtime_error("Unsupported PMX version; expected 2.0 or 2.1");
    if (header[8] != 8 || header[9] > 1 || header[10] > 4)
        throw std::runtime_error("Invalid PMX header size, encoding or additional UV count");
    for (std::size_t i = 11; i < header.size(); ++i)
        if (header[i] != 1 && header[i] != 2 && header[i] != 4)
            throw std::runtime_error("Invalid PMX index width");
}
std::string percentDecode(std::string_view uri) {
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string result;
    result.reserve(uri.size());
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] != '%') {
            if (uri[i] == '\0') throw std::runtime_error("NUL is not valid in a resource URI");
            result += uri[i];
            continue;
        }
        if (i + 2 >= uri.size() || hex(uri[i + 1]) < 0 || hex(uri[i + 2]) < 0)
            throw std::runtime_error("Invalid percent escape in resource URI: " + std::string(uri));
        const auto value = static_cast<char>(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
        if (value == '\0') throw std::runtime_error("NUL is not valid in a resource URI");
        result += value;
        i += 2;
    }
    return result;
}
std::string genericUtf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
// Assimp otherwise uses narrow CRT file APIs on Windows. Opening through path
// keeps the full Unicode filename, including sidecar material/buffer files.
class FileStream final : public Assimp::IOStream {
public:
    explicit FileStream(const std::filesystem::path& path) : file_(path, std::ios::binary) {
        if (file_) {
            file_.seekg(0, std::ios::end);
            const auto end = file_.tellg();
            if (end < 0) {
                file_.close();
                return;
            }
            size_ = static_cast<std::size_t>(end);
            file_.seekg(0);
        }
    }
    bool good() const { return file_.is_open(); }
    std::size_t Read(void* buffer, std::size_t size, std::size_t count) override {
        if (size == 0 || count > std::numeric_limits<std::size_t>::max() / size) return 0;
        const auto bytes = std::min(size * count, size_ - std::min(position_, size_));
        file_.read(static_cast<char*>(buffer), static_cast<std::streamsize>(bytes));
        const auto read = static_cast<std::size_t>(file_.gcount());
        position_ += read;
        return read / size;
    }
    std::size_t Write(const void*, std::size_t, std::size_t) override { return 0; }
    aiReturn Seek(std::size_t offset, aiOrigin origin) override {
        // The Assimp API represents negative fseek offsets in size_t.
        const auto delta = static_cast<std::int64_t>(offset);
        std::size_t base = 0;
        if (origin == aiOrigin_CUR)
            base = position_;
        else if (origin == aiOrigin_END)
            base = size_;
        else if (origin != aiOrigin_SET)
            return aiReturn_FAILURE;
        if (delta < 0 && delta < -static_cast<std::int64_t>(base)) return aiReturn_FAILURE;
        if (delta > 0 && static_cast<std::uint64_t>(delta) > size_ - base) return aiReturn_FAILURE;
        const auto next = static_cast<std::size_t>(static_cast<std::int64_t>(base) + delta);
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(next));
        if (!file_) return aiReturn_FAILURE;
        position_ = next;
        return aiReturn_SUCCESS;
    }
    std::size_t Tell() const override { return position_; }
    std::size_t FileSize() const override { return size_; }
    void Flush() override {}

private:
    std::ifstream file_;
    std::size_t size_{0}, position_{0};
};
class FileSystem final : public Assimp::IOSystem {
public:
    explicit FileSystem(const std::filesystem::path& source)
        : source_(source), directory_(source.parent_path()), gltf_(isGltf(source)) {
        directoryPrefix_ = genericUtf8(directory_);
        if (!directoryPrefix_.ends_with('/')) directoryPrefix_ += '/';
    }
    bool Exists(const char* path) const override {
        try {
            std::error_code ec;
            return std::filesystem::is_regular_file(resolve(path), ec);
        } catch (const std::runtime_error&) {
            return false;
        }
    }
    char getOsSeparator() const override { return '/'; }
    Assimp::IOStream* Open(const char* path, const char* mode = "rb") override {
        if (!mode || mode[0] != 'r') return nullptr;
        try {
            auto file = std::make_unique<FileStream>(resolve(path));
            return file->good() ? file.release() : nullptr;
        } catch (const std::runtime_error&) {
            return nullptr;
        }
    }
    void Close(Assimp::IOStream* stream) override { delete stream; }

private:
    std::filesystem::path resolve(std::string_view name) const {
        const auto raw = pathFromUtf8(name);
        if (!gltf_ || raw.lexically_normal() == source_) return raw;
        // Assimp prepends the model directory to external buffer URIs. Decode
        // only the reference suffix: the model path is a literal filesystem
        // path and can itself contain names such as "literal%20directory".
        const auto request = genericUtf8(raw);
        if (request.starts_with(directoryPrefix_)) {
            auto reference = std::string_view(request).substr(directoryPrefix_.size());
            // Assimp can append '/' to a directory already ending in '\\'.
            // Remove the duplicate separator so path joining stays relative.
            while (reference.starts_with('/')) reference.remove_prefix(1);
            return directory_ / pathFromUtf8(percentDecode(reference));
        }
        if (raw.is_relative()) return directory_ / pathFromUtf8(percentDecode(request));
        return raw;
    }
    std::filesystem::path source_, directory_;
    std::string directoryPrefix_;
    bool gltf_;
};
bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool finite(const glm::vec4& value) { return finite(glm::vec3(value)) && std::isfinite(value.w); }
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
    const auto path = directory / pathFromUtf8(relative);
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
std::size_t ModelData::vertexCount() const {
    std::size_t count = 0;
    for (const auto& mesh : meshes) count += mesh.vertices.size();
    return count;
}
std::size_t ModelData::triangleCount() const {
    std::size_t count = 0;
    for (const auto& mesh : meshes) count += mesh.indices.size() / 3;
    return count;
}
ModelData loadModel(const std::filesystem::path& input) {
    std::error_code ec;
    const auto path = std::filesystem::absolute(input, ec).lexically_normal();
    if (ec || !std::filesystem::is_regular_file(path, ec))
        throw std::runtime_error("Model file does not exist: " + utf8(input));
    // Match Assimp's content sniffing so renamed PMX files cannot bypass the
    // header checks or lose their material/opacity policy.
    const bool pmx = lowerExtension(path) == ".pmx" || hasPmxSignature(path);
    if (pmx) validatePmxHeader(path);
    Assimp::Importer importer;
    importer.SetIOHandler(new FileSystem(path));
    // Assimp normalizes UVs to a bottom-left origin (glTF2Importer flips Y).
    // Our uploaded stb image rows start at the top, so normalize back here.
    const auto* scene = importer.ReadFile(
        utf8(path), aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
                        aiProcess_PreTransformVertices | aiProcess_ValidateDataStructure | aiProcess_FlipUVs);
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
        throw std::runtime_error("Cannot import " + utf8(path.filename()) + ": " + importer.GetErrorString());
    ModelData model;
    model.name = utf8(path.filename());
    model.source = path;
    if (pmx)
        model.warnings.emplace_back("PMX is displayed in its static rest pose; animation, morphs and physics are not applied");
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        const auto& source = *scene->mMaterials[i];
        MaterialData material;
        aiString name;
        source.Get(AI_MATKEY_NAME, name);
        material.name = name.C_Str();
        aiColor4D color(1, 1, 1, 1);
        if (source.Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS) source.Get(AI_MATKEY_COLOR_DIFFUSE, color);
        material.baseColor = {color.r, color.g, color.b, color.a};
        if (!finite(material.baseColor)) throw std::runtime_error("Model contains a non-finite material color");
        if (pmx) {
            // Assimp's MMD importer stores diffuse RGB and opacity separately.
            // Cutouts also apply to texture alpha when diffuse opacity is one.
            float opacity = 1;
            source.Get(AI_MATKEY_OPACITY, opacity);
            if (!std::isfinite(opacity) || opacity < 0 || opacity > 1)
                throw std::runtime_error("PMX material contains an invalid opacity");
            material.baseColor.a = opacity;
            material.alphaMasked = true;
            material.alphaCutoff = 0.1f;
            if (opacity > 0 && opacity < 1)
                model.warnings.push_back("PMX material '" + material.name +
                                         "' uses partial opacity; using alpha cutout (blending unavailable)");
        }
        aiString alphaMode;
        if (source.Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS) {
            const std::string_view mode(alphaMode.C_Str());
            if (mode == "MASK") {
                material.alphaMasked = true;
                source.Get(AI_MATKEY_GLTF_ALPHACUTOFF, material.alphaCutoff);
                if (!std::isfinite(material.alphaCutoff) || material.alphaCutoff < 0)
                    throw std::runtime_error("Model contains an invalid alpha cutoff");
            } else if (mode == "BLEND") {
                model.warnings.push_back("Material '" + material.name +
                                         "' uses unsupported BLEND alpha; using opaque fallback");
            }
        }
        aiString texture;
        if (source.GetTexture(aiTextureType_BASE_COLOR, 0, &texture) == AI_SUCCESS ||
            source.GetTexture(aiTextureType_DIFFUSE, 0, &texture) == AI_SUCCESS) {
            try {
                material.texture = readTexture(*scene, texture, path.parent_path(), isGltf(path));
                material.hasTexture = true;
            } catch (const std::exception& e) {
                model.warnings.push_back("Texture '" + std::string(texture.C_Str()) +
                                         "' unavailable; using white: " + e.what());
            }
        }
        model.materials.push_back(std::move(material));
    }
    if (model.materials.empty()) model.materials.emplace_back();
    glm::vec3 minimum(std::numeric_limits<float>::max()), maximum(std::numeric_limits<float>::lowest());
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const auto& source = *scene->mMeshes[i];
        MeshData mesh;
        mesh.name = source.mName.C_Str();
        mesh.materialIndex = source.mMaterialIndex;
        if (mesh.materialIndex >= model.materials.size())
            throw std::runtime_error("Mesh references an invalid material");
        for (unsigned f = 0; f < source.mNumFaces; ++f) {
            const auto& face = source.mFaces[f];
            if (face.mNumIndices != 3) continue;
            for (unsigned k = 0; k < 3; ++k) {
                if (face.mIndices[k] >= source.mNumVertices)
                    throw std::runtime_error("Mesh contains an invalid vertex index");
                mesh.indices.push_back(face.mIndices[k]);
            }
        }
        if (mesh.indices.empty()) continue;
        mesh.vertices.reserve(source.mNumVertices);
        for (unsigned v = 0; v < source.mNumVertices; ++v) {
            Vertex vertex;
            const auto& p = source.mVertices[v];
            vertex.position = {p.x, p.y, p.z};
            if (source.HasNormals()) {
                const auto& n = source.mNormals[v];
                vertex.normal = {n.x, n.y, n.z};
            }
            if (source.HasTextureCoords(0)) {
                const auto& uv = source.mTextureCoords[0][v];
                vertex.uv = {uv.x, uv.y};
            }
            if (!finite(vertex.position) || !finite(vertex.normal) || !std::isfinite(vertex.uv.x) ||
                !std::isfinite(vertex.uv.y))
                throw std::runtime_error("Model contains non-finite geometry");
            const auto normalLength = glm::length(vertex.normal);
            if (!std::isfinite(normalLength))
                throw std::runtime_error("Model contains an unsupported normal magnitude");
            vertex.normal = normalLength > 0.000001f ? vertex.normal / normalLength : glm::vec3(0, 1, 0);
            minimum = glm::min(minimum, vertex.position);
            maximum = glm::max(maximum, vertex.position);
            mesh.vertices.push_back(vertex);
        }
        model.meshes.push_back(std::move(mesh));
    }
    if (model.meshes.empty()) throw std::runtime_error("Model contains no triangles");
    if (!finite(maximum - minimum) || !std::isfinite(glm::length(maximum - minimum)))
        throw std::runtime_error("Model bounds exceed supported coordinate range");
    model.bounds.min = minimum;
    model.bounds.max = maximum;
    return model;
}
}
