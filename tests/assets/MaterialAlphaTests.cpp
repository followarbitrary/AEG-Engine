#include <MaterialProperties.hpp>
#include <MaterialTextures.hpp>
#include <ModelImporter.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace aeg::assets;
namespace fs = std::filesystem;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Temporary {
    fs::path path =
        fs::temp_directory_path() /
        ("aeg-material-alpha-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() {
        if (!fs::create_directory(path)) throw std::runtime_error("Cannot create unique alpha test directory");
    }
    ~Temporary() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read alpha test fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void write(const fs::path& path, std::string_view data) {
    std::ofstream output(path, std::ios::binary);
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!output) throw std::runtime_error("Cannot write alpha test fixture");
}
void tga(const fs::path& path, unsigned width, unsigned height, const std::vector<std::uint8_t>& rgba) {
    require(width * height * 4 == rgba.size(), "Invalid original test image");
    std::string image(18, '\0');
    image[2] = 2;
    image[12] = static_cast<char>(width & 255);
    image[13] = static_cast<char>(width >> 8);
    image[14] = static_cast<char>(height & 255);
    image[15] = static_cast<char>(height >> 8);
    image[16] = 32;
    image[17] = 0x28;
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        image.push_back(static_cast<char>(rgba[i + 2]));
        image.push_back(static_cast<char>(rgba[i + 1]));
        image.push_back(static_cast<char>(rgba[i]));
        image.push_back(static_cast<char>(rgba[i + 3]));
    }
    write(path, image);
}
fs::path fixture(std::string_view name) { return fs::path(AEG_TEST_FIXTURES) / name; }
ModelData obj(const fs::path& directory, std::string_view properties, bool uv = true) {
    write(directory / "model.mtl", "newmtl Alpha\nKd 0.2 0.4 0.8\n" + std::string(properties));
    const std::string coordinates = uv ? "vt 0 0\nvt 1 0\nvt 0 1\n" : "";
    const std::string face = uv ? "f 1/1/1 2/2/1 3/3/1\n" : "f 1//1 2//1 3//1\n";
    write(directory / "model.obj",
          "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\n" + coordinates + "usemtl Alpha\n" + face);
    return loadModel(directory / "model.obj");
}
const MaterialData& used(const ModelData& model) { return model.materials.at(model.meshes.at(0).materialIndex); }
bool warning(const ModelData& model, std::string_view text) {
    return std::any_of(model.warnings.begin(), model.warnings.end(),
                       [&](const auto& item) { return item.find(text) != std::string::npos; });
}
template <class T> void add(std::string& out, const T& value) {
    out.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
void addString(std::string& out, std::string_view text) {
    add(out, text.size());
    out.append(text);
}
void addPath(std::string& out, const fs::path& path) {
    const auto text = path.u8string();
    addString(out, {reinterpret_cast<const char*>(text.data()), text.size()});
}
void addImage(std::string& out, const ImageData& image) {
    add(out, image.width);
    add(out, image.height);
    add(out, image.rgba.size());
    out.append(reinterpret_cast<const char*>(image.rgba.data()), image.rgba.size());
}
template <class V> void addVector(std::string& out, const V& vector) {
    for (std::size_t i = 0; i < V::component_count; ++i) add(out, vector[i]);
}
std::string snapshot(const ModelData& model) {
    std::string out;
    addString(out, model.name);
    addPath(out, model.source);
    addVector(out, model.bounds.min);
    addVector(out, model.bounds.max);
    add(out, model.warnings.size());
    for (const auto& text : model.warnings) addString(out, text);
    add(out, model.materials.size());
    for (const auto& material : model.materials) {
        addString(out, material.name);
        addVector(out, material.baseColor);
        addImage(out, material.texture);
        add(out, material.hasTexture);
        add(out, material.alphaMode);
        add(out, material.alphaCutoff);
        addImage(out, material.normalTexture);
        add(out, material.hasNormalMap);
        add(out, material.normalScale);
        addString(out, material.normalTextureSource);
        addPath(out, material.baseTexturePath);
        add(out, material.normalTextureDeclared);
    }
    add(out, model.meshes.size());
    for (const auto& mesh : model.meshes) {
        addString(out, mesh.name);
        add(out, mesh.materialIndex);
        add(out, mesh.hasUV0);
        add(out, mesh.indices.size());
        for (const auto index : mesh.indices) add(out, index);
        add(out, mesh.vertices.size());
        for (const auto& vertex : mesh.vertices) {
            addVector(out, vertex.position);
            addVector(out, vertex.normal);
            addVector(out, vertex.uv);
            addVector(out, vertex.tangent);
        }
    }
    return out;
}
template <class Operation> void failurePreserves(ModelData model, Operation operation) {
    const auto before = snapshot(model);
    bool rejected{};
    try {
        operation(model);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Invalid alpha operation must throw");
    require(snapshot(model) == before, "Failed alpha operation preserves every semantic model field");
}
void append32(std::string& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<char>(value >> (i * 8)));
}
void glb(const fs::path& path, std::string json) {
    // Build a genuine GLB with buffer zero in its BIN chunk. Keeping the glTF
    // data URI while omitting BIN does not exercise the binary container path.
    const std::string_view prefix = "\"uri\": \"data:application/octet-stream;base64,";
    const auto uri = json.find(prefix);
    require(uri != std::string::npos, "Original glTF fixture has an embedded geometry buffer");
    const auto start = uri + prefix.size(), end = json.find('"', start);
    require(end != std::string::npos, "Geometry data URI terminates");
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string binary;
    std::uint32_t accumulator{};
    unsigned bits{};
    for (const char character : std::string_view(json).substr(start, end - start)) {
        if (character == '=') break;
        const auto value = alphabet.find(character);
        require(value != std::string_view::npos, "Geometry buffer uses valid Base64");
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            binary.push_back(static_cast<char>(accumulator >> bits));
        }
    }
    json.erase(uri, end - uri + 1);
    require(json[uri] == ',', "Geometry buffer URI precedes its byte length");
    json.erase(uri, 1);
    while (binary.size() % 4) binary.push_back('\0');
    while (json.size() % 4) json.push_back(' ');
    std::string bytes;
    append32(bytes, 0x46546c67);
    append32(bytes, 2);
    append32(bytes, static_cast<std::uint32_t>(28 + json.size() + binary.size()));
    append32(bytes, static_cast<std::uint32_t>(json.size()));
    append32(bytes, 0x4e4f534a);
    bytes += json;
    append32(bytes, static_cast<std::uint32_t>(binary.size()));
    append32(bytes, 0x004e4942);
    bytes += binary;
    write(path, bytes);
}
void gltfModes() {
    Temporary temporary;
    for (const auto& item : std::array{
             std::pair{"alpha-opaque.gltf", AlphaMode::Opaque}, std::pair{"alpha-default.gltf", AlphaMode::Opaque},
             std::pair{"alpha-mask.gltf", AlphaMode::Mask}, std::pair{"alpha-blend.gltf", AlphaMode::Blend}}) {
        const auto input = fixture(item.first);
        const auto model = loadModel(input);
        require(used(model).alphaMode == item.second && model.warnings.empty(),
                "glTF preserves the declared alpha mode");
        glb(temporary.path / "model.glb", read(input));
        const auto binary = loadModel(temporary.path / "model.glb");
        require(used(binary).alphaMode == item.second && binary.warnings.empty(), "GLB uses the same alpha policy");
        const float expected = item.second == AlphaMode::Opaque ? 0.0f : 0.3f;
        require(std::abs(used(model).baseColor.w - expected) < 0.000001f &&
                    std::abs(used(binary).baseColor.w - expected) < 0.000001f,
                "glTF/GLB alpha factor is not multiplied by the duplicate Assimp opacity property");
    }
    const auto custom = loadModel(fixture("alpha-mask-custom.gltf"));
    require(std::abs(used(custom).alphaCutoff - 0.1f) < 0.000001f, "Explicit MASK cutoff survives import");
}
void objFactorsAndTextureModes() {
    Temporary temporary;
    for (const auto text : {"d 0.25\n", "Tr 0.75\n"}) {
        const auto model = obj(temporary.path, text);
        require(used(model).alphaMode == AlphaMode::Blend && std::abs(used(model).baseColor.w - 0.25f) < 0.000001f,
                "OBJ d and Tr map to the same unsquared opacity factor");
        require(model.warnings.empty(), "OBJ scalar opacity needs no approximation warning");
    }
    for (const auto value : {"d 0\n", "Tr 1\n"}) {
        const auto model = obj(temporary.path, value);
        require(used(model).alphaMode == AlphaMode::Blend && used(model).baseColor.w == 0,
                "Zero OBJ opacity is retained as Blend");
    }
    const std::array modes{AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend};
    const std::array<std::array<std::uint8_t, 2>, 3> alphas{{{{255, 255}}, {{0, 255}}, {{64, 255}}}};
    for (std::size_t i = 0; i < modes.size(); ++i) {
        tga(temporary.path / "base.tga", 2, 1, {10, 20, 30, alphas[i][0], 40, 50, 60, alphas[i][1]});
        const auto model = obj(temporary.path, "d 1\nmap_Kd base.tga\n");
        require(used(model).alphaMode == modes[i] && used(model).alphaCutoff == 0.5f,
                "Generic imported RGBA chooses opaque, binary mask, or partial blend");
    }
    for (const auto invalid : {"d -0.1\n", "d 1.1\n", "Tr 1.1\n"}) {
        bool rejected{};
        try {
            (void)obj(temporary.path, invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Out-of-range OBJ opacity is rejected");
    }
}
void opacityMaps() {
    Temporary temporary;
    tga(temporary.path / "base.tga", 2, 1, {10, 20, 30, 64, 40, 50, 60, 128});
    // R is deliberately different from G/B and image alpha is always opaque.
    tga(temporary.path / "opacity.tga", 1, 2, {64, 200, 30, 255, 128, 10, 240, 255});
    const auto model = obj(temporary.path, "map_Kd base.tga\nmap_d opacity.tga\n");
    const auto& material = used(model);
    require(material.hasTexture && material.texture.width == 2 && material.texture.height == 2,
            "Opacity maps use bounded normalized-UV resampling at the combined resolution");
    require(material.texture.rgba ==
                std::vector<std::uint8_t>({10, 20, 30, 16, 40, 50, 60, 32, 10, 20, 30, 32, 40, 50, 60, 64}),
            "map_d multiplies base alpha by linear R and preserves sampled RGB");
    require(material.alphaMode == AlphaMode::Blend && model.warnings.empty(),
            "Partial opacity-map values select blending");
    const auto only = obj(temporary.path, "map_d opacity.tga\n");
    require(used(only).texture.rgba == std::vector<std::uint8_t>({255, 255, 255, 64, 255, 255, 255, 128}),
            "An opacity map without a base image uses white RGB at opacity resolution");
    const auto original = obj(temporary.path, "map_Kd base.tga\n");
    for (const auto properties :
         {"map_Kd base.tga\nmap_d absent.tga\n", "map_Kd base.tga\nmap_d -s 2 2 1 opacity.tga\n",
          "map_Kd base.tga\nmap_d -imfchan a opacity.tga\n"}) {
        const auto fallback = obj(temporary.path, properties);
        require(used(fallback).texture.rgba == used(original).texture.rgba &&
                    used(fallback).texture.width == used(original).texture.width &&
                    used(fallback).texture.height == used(original).texture.height &&
                    warning(fallback, "Opacity texture"),
                "Missing or unsupported opacity maps diagnose and preserve the original base image");
    }
    const auto noUV = obj(temporary.path, "map_Kd base.tga\nmap_d opacity.tga\n", false);
    require(used(noUV).texture.rgba == used(original).texture.rgba && warning(noUV, "UV0"),
            "Opacity composition requires actual UV0 and retains base data on unsupported coordinates");
}
void pmxPolicies() {
    Temporary temporary;
    fs::create_directory(temporary.path / "pmx-textures");
    fs::copy_file(fixture("pmx-textures/cutout.png"), temporary.path / "pmx-textures/cutout.png");
    auto bytes = read(fixture("pmx-alpha.pmx"));
    const auto partial = loadModel(fixture("pmx-alpha.pmx"));
    require(used(partial).alphaMode == AlphaMode::Blend && std::abs(used(partial).baseColor.w - 0.4f) < 0.000001f,
            "PMX separate diffuse opacity selects blending without being squared");
    require(!warning(partial, "partial opacity") && !warning(partial, "unavailable"),
            "PMX no longer warns that alpha blending is unavailable");
    const std::array<float, 4> factor{0.25f, 0.5f, 0.75f, 0.4f};
    const std::string marker(reinterpret_cast<const char*>(factor.data()), sizeof(factor));
    const auto offset = bytes.find(marker);
    require(offset != std::string::npos && bytes.find(marker, offset + 1) == std::string::npos,
            "Original PMX fixture has one known diffuse factor to vary");
    const float opaque = 1;
    std::memcpy(bytes.data() + offset + 12, &opaque, sizeof(opaque));
    write(temporary.path / "renamed.bin", bytes);
    const auto binary = loadModel(temporary.path / "renamed.bin");
    require(used(binary).alphaMode == AlphaMode::Mask && std::abs(used(binary).alphaCutoff - 0.1f) < 0.000001f,
            "Signature-detected PMX binary texture alpha keeps its legacy cutoff");
    tga(temporary.path / "pmx-textures/cutout.png", 1, 1, {10, 20, 30, 128});
    const auto translucent = loadModel(temporary.path / "renamed.bin");
    require(used(translucent).alphaMode == AlphaMode::Blend, "PMX partial texture alpha selects blending");
    const auto solid = loadModel(fixture("pmx-utf8.pmx"));
    require(used(solid).alphaMode == AlphaMode::Opaque, "PMX fully opaque material stays in the opaque pass");
}
ModelData procedural() {
    ModelData model;
    model.name = "Alpha transaction";
    model.materials.resize(2);
    model.materials[0].name = "Selected";
    model.materials[1].name = "Other";
    model.warnings = {"Retained notice"};
    MeshData mesh;
    mesh.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}}, {{1, 0, 0}, {0, 0, 1}, {1, 0}}, {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
    mesh.indices = {0, 1, 2};
    model.meshes.push_back(mesh);
    return model;
}
void propertyTransactions() {
    auto model = procedural();
    for (const auto mode : {AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend}) {
        auto expected = model;
        expected.materials[0].alphaMode = mode;
        expected.materials[0].baseColor.w = 0.25f;
        expected.materials[0].alphaCutoff = 1.25f;
        setMaterialAlpha(model, 0, mode, 0.25f, 1.25f);
        require(snapshot(model) == snapshot(expected), "Alpha editing changes only selected mode, factor, and cutoff");
    }
    for (const auto invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity(), -0.01f, 1.01f}) {
        failurePreserves(model, [&](auto& value) { setMaterialAlpha(value, 0, AlphaMode::Blend, invalid); });
        auto bad = model;
        bad.materials[1].baseColor.w = invalid;
        failurePreserves(bad, [](auto& value) { prepareModelMaterials(value); });
    }
    for (const auto invalid :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -0.01f}) {
        failurePreserves(model, [&](auto& value) { setMaterialAlpha(value, 0, AlphaMode::Mask, 1, invalid); });
        auto bad = model;
        bad.materials[0].alphaCutoff = invalid;
        failurePreserves(bad, [](auto& value) { prepareModelMaterials(value); });
    }
    const auto invalidMode = static_cast<AlphaMode>(999);
    failurePreserves(model, [&](auto& value) { setMaterialAlpha(value, 0, invalidMode, 1); });
    failurePreserves(model, [&](auto& value) { setMaterialAlpha(value, value.materials.size(), AlphaMode::Blend, 1); });
    auto invalid = model;
    invalid.materials[0].alphaMode = invalidMode;
    failurePreserves(invalid, [](auto& value) { prepareModelMaterials(value); });
    failurePreserves(invalid, [](auto& value) { initializeModelMaterials(value); });
    setMaterialAlpha(model, 0, AlphaMode::Opaque, 0);
    prepareModelMaterials(model);
    require(model.materials[0].alphaMode == AlphaMode::Opaque && model.materials[0].baseColor.w == 0,
            "Generic material preparation does not reinterpret an explicit procedural Opaque policy");
    setMaterialAlpha(model, 0, AlphaMode::Blend, 0.35f, 0.2f);
    setMaterialNormalMap(model, 0, ImageData{1, 1, {128, 128, 255, 255}}, 0.8f);
    require(model.materials[0].alphaMode == AlphaMode::Blend && model.materials[0].baseColor.w == 0.35f &&
                model.materials[0].alphaCutoff == 0.2f,
            "Normal assignment preserves alpha properties");
    clearMaterialNormalMap(model, 0);
    require(model.materials[0].alphaMode == AlphaMode::Blend && model.materials[0].baseColor.w == 0.35f &&
                model.materials[0].alphaCutoff == 0.2f,
            "Normal clearing preserves alpha properties");
}
}
int main() {
    int failures{};
    for (const auto& [name, test] : std::array<std::pair<const char*, std::function<void()>>, 5>{
             {{"glTF and GLB explicit alpha modes", gltfModes},
              {"OBJ factors and RGBA modes", objFactorsAndTextureModes},
              {"OBJ opacity maps and fallback", opacityMaps},
              {"PMX opacity and signature policy", pmxPolicies},
              {"Material alpha transactions", propertyTransactions}}}) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
