#include <MaterialProperties.hpp>
#include <MaterialTextures.hpp>
#include <ModelImporter.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace aeg::assets;
namespace fs = std::filesystem;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a - b) < 0.000001f; }
struct Temporary {
    fs::path path = fs::temp_directory_path() /
                    ("aeg-material-pbr-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() {
        if (!fs::create_directory(path)) throw std::runtime_error("Cannot create unique PBR test directory");
    }
    ~Temporary() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};
void write(const fs::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("Cannot write PBR test fixture");
}
void append32(std::string& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<char>(value >> (8 * i)));
}
// Build real glTF and GLB containers with identical geometry and material JSON.
// GLB embeds its positions in BIN rather than reusing an external/data URI.
ModelData native(const fs::path& directory, std::string_view material, bool binary, bool texture = false,
                 bool declareMaterial = true) {
    std::string positions;
    for (const float value : {-1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f})
        append32(positions, std::bit_cast<std::uint32_t>(value));
    std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":36)";
    if (!binary) json += R"(,"uri":"triangle.bin")";
    json +=
        R"(}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,-1,0],"max":[1,1,0]}])";
    if (declareMaterial) json += ",\"materials\":[" + std::string(material) + "]";
    if (texture) json += R"(,"textures":[{"source":0}],"images":[{"uri":"deliberately-unread-pbr.png"}])";
    json += R"(,"meshes":[{"primitives":[{"attributes":{"POSITION":0})";
    if (declareMaterial) json += R"(,"material":0)";
    json += R"(}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    const auto path = directory / (binary ? "model.glb" : "model.gltf");
    if (binary) {
        while (json.size() % 4) json.push_back(' ');
        std::string bytes;
        append32(bytes, 0x46546c67);
        append32(bytes, 2);
        append32(bytes, static_cast<std::uint32_t>(28 + json.size() + positions.size()));
        append32(bytes, static_cast<std::uint32_t>(json.size()));
        append32(bytes, 0x4e4f534a);
        bytes += json;
        append32(bytes, static_cast<std::uint32_t>(positions.size()));
        append32(bytes, 0x004e4942);
        bytes += positions;
        write(path, bytes);
    } else {
        write(directory / "triangle.bin", positions);
        write(path, json);
    }
    return loadModel(path);
}
const MaterialData& used(const ModelData& model) { return model.materials.at(model.meshes.at(0).materialIndex); }
ModelData obj(const fs::path& directory, std::string_view properties) {
    write(directory / "model.mtl", "newmtl Surface\nKd 0.2 0.4 0.8\n" + std::string(properties));
    write(directory / "model.obj", "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Surface\nf 1 2 3\n");
    return loadModel(directory / "model.obj");
}
template <class T> void add(std::string& out, const T& value) {
    out.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
void addString(std::string& out, std::string_view value) {
    add(out, value.size());
    out.append(value);
}
void addPath(std::string& out, const fs::path& path) {
    const auto value = path.u8string();
    addString(out, {reinterpret_cast<const char*>(value.data()), value.size()});
}
template <class V> void addVector(std::string& out, const V& value) {
    for (std::size_t i = 0; i < V::component_count; ++i) add(out, value[i]);
}
void addImage(std::string& out, const ImageData& image) {
    add(out, image.width);
    add(out, image.height);
    add(out, image.rgba.size());
    out.append(reinterpret_cast<const char*>(image.rgba.data()), image.rgba.size());
}
// Compare every semantic field, including NaN payloads, but never Vec3 padding.
std::string snapshot(const ModelData& model) {
    std::string out;
    addString(out, model.name);
    addPath(out, model.source);
    addVector(out, model.bounds.min);
    addVector(out, model.bounds.max);
    add(out, model.warnings.size());
    for (const auto& warning : model.warnings) addString(out, warning);
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
        add(out, material.metallicFactor);
        add(out, material.roughnessFactor);
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
    require(rejected, "Invalid PBR properties must throw");
    require(snapshot(model) == before, "Failed PBR operation preserves every semantic model field");
}
ModelData procedural() {
    ModelData model;
    model.name = "PBR transaction";
    model.materials.resize(2);
    model.materials[0].name = "Selected";
    model.materials[1].name = "Other";
    model.materials[1].metallicFactor = 0.25f;
    model.materials[1].roughnessFactor = 0.75f;
    model.warnings = {"Retained notice"};
    MeshData mesh;
    mesh.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}}, {{1, 0, 0}, {0, 0, 1}, {1, 0}}, {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
    mesh.indices = {0, 1, 2};
    model.meshes.push_back(mesh);
    return model;
}
void legacyDefaults() {
    auto model = procedural();
    prepareModelMaterials(model);
    require(used(model).metallicFactor == 0 && used(model).roughnessFactor == 0.5f,
            "Procedural materials retain nonmetallic roughness 0.5 through preparation");
    Temporary temporary;
    const auto old = obj(temporary.path, "Ns 1000\nKs 1 1 1\n");
    require(used(old).metallicFactor == 0 && used(old).roughnessFactor == 0.5f && old.warnings.empty(),
            "Legacy OBJ does not infer metalness or roughness from shininess");
    const auto pmx = loadModel(fs::path(AEG_TEST_FIXTURES) / "pmx-utf8.pmx");
    require(used(pmx).metallicFactor == 0 && used(pmx).roughnessFactor == 0.5f,
            "PMX inherits the same legacy PBR defaults");
    const auto explicitObj = obj(temporary.path, "Pm 0.35\nPr 0.65\n");
    require(close(used(explicitObj).metallicFactor, 0.35f) && close(used(explicitObj).roughnessFactor, 0.65f),
            "Native OBJ PBR factors use the common Assimp properties");
}
void nativeFactors() {
    Temporary temporary;
    for (const bool binary : {false, true}) {
        for (const auto material : {"{}", R"({"pbrMetallicRoughness":{}})"}) {
            const auto model = native(temporary.path, material, binary);
            require(used(model).metallicFactor == 1 && used(model).roughnessFactor == 1 && model.warnings.empty(),
                    "glTF and GLB omitted factors preserve native defaults rather than legacy defaults");
        }
        const auto missing = native(temporary.path, "", binary, false, false);
        require(used(missing).metallicFactor == 1 && used(missing).roughnessFactor == 1,
                "glTF primitive with no material preserves native default PBR material");
        const auto custom =
            native(temporary.path, R"({"pbrMetallicRoughness":{"metallicFactor":0.3,"roughnessFactor":0.7}})", binary);
        require(close(used(custom).metallicFactor, 0.3f) && close(used(custom).roughnessFactor, 0.7f) &&
                    custom.warnings.empty(),
                "glTF and real GLB preserve independent explicit scalar factors");
        for (const auto material : {R"({"pbrMetallicRoughness":{"metallicFactor":0,"roughnessFactor":1}})",
                                    R"({"pbrMetallicRoughness":{"metallicFactor":1,"roughnessFactor":0}})"}) {
            const auto boundary = native(temporary.path, material, binary);
            require(used(boundary).metallicFactor + used(boundary).roughnessFactor == 1,
                    "Native zero and one PBR factors are not clamped to hidden material values");
        }
        for (const auto invalid : {R"({"pbrMetallicRoughness":{"metallicFactor":-0.1}})",
                                   R"({"pbrMetallicRoughness":{"roughnessFactor":1.1}})"}) {
            bool rejected{};
            try {
                (void)native(temporary.path, invalid, binary);
            } catch (const std::exception&) {
                rejected = true;
            }
            require(rejected, "Out-of-range native PBR factors are rejected instead of silently remapped");
        }
    }
}
void textureFallback() {
    Temporary temporary;
    for (const bool binary : {false, true}) {
        const auto model = native(
            temporary.path,
            R"({"name":"Packed PBR","pbrMetallicRoughness":{"metallicFactor":0.2,"roughnessFactor":0.8,"metallicRoughnessTexture":{"index":0}}})",
            binary, true);
        require(close(used(model).metallicFactor, 0.2f) && close(used(model).roughnessFactor, 0.8f),
                "Unsupported native MR map retains its scalar factors");
        require(model.warnings.size() == 1 &&
                    model.warnings.front().find("metallic/roughness texture") != std::string::npos &&
                    model.warnings.front().find("Packed PBR") != std::string::npos && !used(model).hasTexture,
                "The shared packed map is diagnosed once and is never mistaken for a base-color image");
    }
    const auto separate = obj(temporary.path, "Pm 0.3\nPr 0.6\nmap_Pm metal.png\nmap_Pr rough.png\n");
    require(close(used(separate).metallicFactor, 0.3f) && close(used(separate).roughnessFactor, 0.6f) &&
                separate.warnings.size() == 1 &&
                separate.warnings.front().find("metallic/roughness texture") != std::string::npos,
            "Other formats diagnose unsupported metallic/roughness maps through the same import policy");
}
void transactions() {
    auto model = procedural();
    for (const auto factors : std::array{std::pair{0.0f, 1.0f}, std::pair{1.0f, 0.0f}, std::pair{0.4f, 0.6f}}) {
        auto expected = model;
        expected.materials[0].metallicFactor = factors.first;
        expected.materials[0].roughnessFactor = factors.second;
        setMaterialPbr(model, 0, factors.first, factors.second);
        require(snapshot(model) == snapshot(expected), "PBR editing changes only the selected two factors");
    }
    for (const auto invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity(), -0.01f, 1.01f}) {
        failurePreserves(model, [&](auto& value) { setMaterialPbr(value, 0, invalid, 0.9f); });
        failurePreserves(model, [&](auto& value) { setMaterialPbr(value, 0, 0.9f, invalid); });
        for (const bool roughness : {false, true}) {
            auto bad = model;
            (roughness ? bad.materials[1].roughnessFactor : bad.materials[1].metallicFactor) = invalid;
            failurePreserves(bad, [](auto& value) { prepareModelMaterials(value); });
            failurePreserves(bad, [](auto& value) { initializeModelMaterials(value); });
        }
    }
    failurePreserves(model, [](auto& value) { setMaterialPbr(value, value.materials.size(), 0, 1); });
    setMaterialAlpha(model, 0, AlphaMode::Blend, 0.35f, 0.2f);
    setMaterialNormalMap(model, 0, ImageData{1, 1, {128, 128, 255, 255}}, 0.8f);
    clearMaterialNormalMap(model, 0);
    require(model.materials[0].metallicFactor == 0.4f && model.materials[0].roughnessFactor == 0.6f,
            "Transparency edits and normal map replacement/clearing preserve PBR factors");
}
}
int main() {
    int failures{};
    for (const auto& [name, test] : std::array<std::pair<const char*, std::function<void()>>, 4>{
             {{"Legacy and explicit OBJ PBR defaults", legacyDefaults},
              {"Native glTF and GLB PBR factors", nativeFactors},
              {"Unsupported MR texture fallback", textureFallback},
              {"Material PBR transactions", transactions}}}) {
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
