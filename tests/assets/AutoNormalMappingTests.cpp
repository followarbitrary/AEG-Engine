#include <MaterialTextures.hpp>
#include <ModelImporter.hpp>
#include <Utf8Path.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace aeg::assets;
namespace fs = std::filesystem;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
fs::path fixture(std::string_view name) { return fs::path(AEG_TEST_FIXTURES) / name; }

class Temporary {
public:
    fs::path root;
    Temporary() {
        parent_ = fs::absolute(fs::temp_directory_path()).lexically_normal();
        std::random_device random;
        for (int attempt = 0; attempt < 32; ++attempt) {
            root = parent_ / ("aeg-auto-normal-%20+-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                              std::to_string(random()));
            if (fs::create_directory(root)) return;
        }
        throw std::runtime_error("Cannot create an owned automatic normal-map test directory");
    }
    ~Temporary() {
        // Only remove the absolute direct child that this instance claimed.
        if (!root.is_absolute() || root.parent_path() != parent_ ||
            !root.filename().string().starts_with("aeg-auto-normal-%20+-"))
            return;
        std::error_code error;
        fs::remove_all(root, error);
    }
    Temporary(const Temporary&) = delete;
    Temporary& operator=(const Temporary&) = delete;
    fs::path copy(const fs::path& relative, std::string_view original = "normal-map.png") const {
        const auto target = root / relative;
        fs::create_directories(target.parent_path());
        fs::copy_file(fixture(original), target);
        return target;
    }
    void text(const fs::path& relative, const std::string& value) const {
        const auto target = root / relative;
        fs::create_directories(target.parent_path());
        std::ofstream file(target, std::ios::binary);
        file.write(value.data(), static_cast<std::streamsize>(value.size()));
        require(bool(file), "test data write failed");
    }

private:
    fs::path parent_;
};

ModelData model(const fs::path& directory, const fs::path& base = "Body.png", std::string name = "Cloth") {
    ModelData result;
    result.name = "Original automatic material fixture";
    result.source = directory / "Scene.future-format";
    auto& material = result.materials.emplace_back();
    material.name = std::move(name);
    material.baseTexturePath = base.empty() ? fs::path{} : directory / base;
    material.hasTexture = !base.empty();
    auto& mesh = result.meshes.emplace_back();
    mesh.name = "Original tangent triangle";
    mesh.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}}, {{1, 0, 0}, {0, 0, 1}, {1, 0}}, {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
    mesh.indices = {0, 1, 2};
    result.bounds = {{0, 0, 0}, {1, 1, 0}};
    return result;
}
void prepared(const ModelData& value, const fs::path& image, std::size_t index = 0) {
    const auto& material = value.materials.at(index);
    require(material.hasNormalMap && material.normalTexture.width == 2 && material.normalTexture.height == 2,
            "matching RGB normal image must be loaded");
    require(material.normalScale == 1, "discovered normal images default to scale one");
    require(material.normalTextureSource == aeg::foundation::pathToUtf8(image.lexically_normal()),
            "automatic material source records the actual resolved image");
    for (const auto& mesh : value.meshes)
        if (mesh.materialIndex == index)
            for (const auto& vertex : mesh.vertices)
                require(std::abs(vertex.tangent.w) == 1 &&
                            std::abs(aeg::math::length(aeg::math::Vec3(vertex.tangent)) - 1) < 0.001f &&
                            std::abs(aeg::math::dot(vertex.normal, aeg::math::Vec3(vertex.tangent))) < 0.001f,
                        "initialization must prepare tangent frames for a discovered map");
}
void unmapped(const ModelData& value, bool quiet = true, std::size_t index = 0) {
    require(!value.materials.at(index).hasNormalMap, "unmatched or ineligible material must retain vertex normals");
    if (quiet) require(value.warnings.empty(), "no candidate is a quiet normal-map fallback");
}
bool sameImage(const ImageData& a, const ImageData& b) {
    return a.width == b.width && a.height == b.height && a.rgba == b.rgba;
}
bool sameMaterial(const MaterialData& a, const MaterialData& b) {
    return a.name == b.name && a.baseColor == b.baseColor && sameImage(a.texture, b.texture) &&
           a.hasTexture == b.hasTexture && a.alphaMasked == b.alphaMasked && a.alphaCutoff == b.alphaCutoff &&
           sameImage(a.normalTexture, b.normalTexture) && a.hasNormalMap == b.hasNormalMap &&
           a.normalScale == b.normalScale && a.normalTextureSource == b.normalTextureSource &&
           a.baseTexturePath == b.baseTexturePath && a.normalTextureDeclared == b.normalTextureDeclared;
}
bool sameModel(const ModelData& a, const ModelData& b) {
    if (a.name != b.name || a.source != b.source || a.warnings != b.warnings || a.bounds.min != b.bounds.min ||
        a.bounds.max != b.bounds.max || a.materials.size() != b.materials.size() || a.meshes.size() != b.meshes.size())
        return false;
    for (std::size_t i = 0; i < a.materials.size(); ++i)
        if (!sameMaterial(a.materials[i], b.materials[i])) return false;
    for (std::size_t i = 0; i < a.meshes.size(); ++i) {
        const auto& left = a.meshes[i];
        const auto& right = b.meshes[i];
        if (left.name != right.name || left.materialIndex != right.materialIndex || left.hasUV0 != right.hasUV0 ||
            left.indices != right.indices || left.vertices.size() != right.vertices.size())
            return false;
        for (std::size_t v = 0; v < left.vertices.size(); ++v)
            if (left.vertices[v].position != right.vertices[v].position ||
                left.vertices[v].normal != right.vertices[v].normal || left.vertices[v].uv != right.vertices[v].uv ||
                left.vertices[v].tangent != right.vertices[v].tangent)
                return false;
    }
    return true;
}

void unicodeAppendedNormal() {
    Temporary files;
    const fs::path base(u8"\u8863.png"), normal(u8"\u8863N.PNG");
    files.copy(base);
    const auto image = files.copy(normal);
    auto value = model(files.root, base);
    initializeModelMaterials(value);
    prepared(value, image);
    require(value.warnings.empty(), "a uniquely matched Chinese appended-N image needs no warning");
}
void commonNamesAndColorSuffixes() {
    Temporary files;
    const std::vector<std::string> names{"Body_normal", "Body_normals",   "Body_n",          "Body_nrm",
                                         "Body_nor",    "Body_normalmap", "Body_normal_map", "BodyNormal",
                                         "BodyN",       "normal_Body",    "n_Body",          "bOdY_NrM"};
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto folder = fs::path(std::to_string(i));
        const auto base = files.copy(folder / "Body.png");
        const auto image = files.copy(folder / (names[i] + ".PnG"));
        auto value = model(base.parent_path());
        initializeModelMaterials(value);
        prepared(value, image);
    }
    for (const auto& suffix : {"_base_color", "_basecolor", "_albedo", "_diffuse", "_color", "_base", "_D"}) {
        const auto folder = fs::path(suffix);
        const auto base = files.copy(folder / ("Body" + std::string(suffix) + ".png"));
        const auto image = files.copy(folder / "body_n.png");
        auto value = model(base.parent_path(), base.filename());
        initializeModelMaterials(value);
        prepared(value, image);
    }
}
void dedicatedNormalDirectories() {
    Temporary files;
    std::size_t index = 0;
    for (const auto& normalDirectory : {"normal", "normals", "normalmap", "normalmaps", "NoRmAlMaP"}) {
        const auto folder = fs::path(std::to_string(index++));
        files.copy(folder / "tex/Body.png");
        const auto image = files.copy(folder / normalDirectory / "Body.png");
        auto value = model(files.root / folder, fs::path("tex/Body.png"));
        initializeModelMaterials(value);
        prepared(value, image);
    }
    files.copy("nested/tex/Body.png");
    const auto image = files.copy("nested/tex/Normals/Body.png");
    auto value = model(files.root / "nested", fs::path("tex/Body.png"));
    initializeModelMaterials(value);
    prepared(value, image);
}
void identityTierPriority() {
    Temporary files;
    files.copy("Body_diffuse.png");
    const auto full = files.copy("Body_diffuse_n.png");
    const auto stripped = files.copy("Body_n.png");
    const auto material = files.copy("Cloth_n.png");
    auto fresh = [&] { return model(files.root, "Body_diffuse.png", "Cloth"); };
    auto value = fresh();
    initializeModelMaterials(value);
    prepared(value, full);
    fs::remove(full);
    value = fresh();
    initializeModelMaterials(value);
    prepared(value, stripped);
    fs::remove(stripped);
    value = fresh();
    initializeModelMaterials(value);
    prepared(value, material);
}
void materialAndModelFallbacks() {
    Temporary files;
    const auto image = files.copy("textures/Body_n.png");
    auto value = model(files.root, {}, "Body");
    initializeModelMaterials(value);
    prepared(value, image);
    const auto loneImage = files.copy("single/Hero_n.png");
    value = model(files.root / "single", {}, "Unmatched");
    value.source = files.root / "single/Hero.glb";
    value.materials.emplace_back().name = "Unused";
    initializeModelMaterials(value);
    prepared(value, loneImage);
    unmapped(value, true, 1);
    auto many = model(files.root / "single", {}, "Unmatched");
    many.source = files.root / "single/Hero.glb";
    many.materials.emplace_back().name = "Different";
    many.meshes.push_back(many.meshes[0]);
    many.meshes.back().materialIndex = 1;
    initializeModelMaterials(many);
    unmapped(many);
    unmapped(many, true, 1);
}
void boundedDirectoryScope() {
    Temporary files;
    files.copy("Body.png");
    files.copy("unrelated/Body_n.png");
    files.copy("unrelated/normalmap/Body.png");
    files.copy("Body_bump.png");
    files.copy("Body_toon.png");
    files.copy("Body_sphere.png");
    auto value = model(files.root);
    initializeModelMaterials(value);
    unmapped(value);
    auto unused = model(files.root);
    unused.materials.emplace_back().name = "Unused";
    files.copy("Unused_n.png");
    initializeModelMaterials(unused);
    unmapped(unused, true, 1);
    files.copy("self/Body_n.png");
    auto self = model(files.root / "self", "Body_n.png", "Body");
    initializeModelMaterials(self);
    unmapped(self);
}
void ambiguousCandidates() {
    Temporary files;
    files.copy("suffix/Body.png");
    files.copy("suffix/Body_n.png");
    files.copy("suffix/Body_normal.png");
    files.copy("suffix/Cloth_n.png");
    auto value = model(files.root / "suffix");
    initializeModelMaterials(value);
    unmapped(value, false);
    require(value.warnings.size() == 1, "ambiguous same-tier suffixes report one notice without lower-tier guessing");
    files.copy("extension/Body.png");
    files.copy("extension/Body_n.png");
    files.copy("extension/Body_n.jpg");
    value = model(files.root / "extension");
    initializeModelMaterials(value);
    unmapped(value, false);
    require(value.warnings.size() == 1, "different image extensions remain ambiguous");
    files.copy("directory/Body.png");
    files.copy("directory/Body_n.png");
    files.copy("directory/normalmap/Body.png");
    value = model(files.root / "directory");
    initializeModelMaterials(value);
    unmapped(value, false);
    require(value.warnings.size() == 1, "different eligible directories remain ambiguous");
}
void corruptAndMissingCandidates() {
    Temporary files;
    files.copy("Body.png");
    files.text("Body_n.png", "This original test file is deliberately not an image.");
    files.copy("Cloth_n.png");
    auto value = model(files.root);
    initializeModelMaterials(value);
    unmapped(value, false);
    require(value.warnings.size() == 1 && value.materials[0].normalTextureSource.empty(),
            "corrupt preferred candidate warns without accepting a lower-confidence map");
    auto missing = model(files.root / "does-not-exist");
    initializeModelMaterials(missing);
    unmapped(missing);
}
void nativeAndManualPrecedence() {
    Temporary files;
    files.copy("Body.png");
    files.copy("Body_n.png");
    auto declared = model(files.root);
    declared.materials[0].normalTextureDeclared = true;
    declared.warnings = {"Original unsupported native normal reference"};
    initializeModelMaterials(declared);
    unmapped(declared, false);
    require(declared.warnings == std::vector<std::string>{"Original unsupported native normal reference"},
            "native missing or unsupported normal reference blocks automatic guesses");
    auto manual = model(files.root);
    setMaterialNormalMap(manual, 0, ImageData{1, 1, {204, 128, 230, 255}}, -0.75f);
    const auto original = manual.materials[0];
    initializeModelMaterials(manual);
    require(sameMaterial(manual.materials[0], original), "initialization never overrides an explicit manual map");
}
void manualClearAndOverride() {
    Temporary files;
    files.copy("Body.png");
    const auto image = files.copy("Body_n.png");
    const auto overrideImage = files.copy("Explicit.png");
    auto value = model(files.root);
    initializeModelMaterials(value);
    prepared(value, image);
    clearMaterialNormalMap(value, 0);
    prepareModelMaterials(value);
    unmapped(value);
    setMaterialNormalMap(value, 0, overrideImage, -0.5f);
    const auto replacement = value.materials[0];
    prepareModelMaterials(value);
    initializeModelMaterials(value);
    require(sameMaterial(value.materials[0], replacement), "manual file replacement remains authoritative");
}
void sourceLessDoesNotScanCwd() {
    Temporary files;
    files.copy("Body_n.png");
    auto value = model(files.root, {}, "Body");
    value.source.clear();
    const auto previous = fs::current_path();
    struct Restore {
        fs::path path;
        ~Restore() {
            std::error_code error;
            fs::current_path(path, error);
        }
    } restore{previous};
    fs::current_path(files.root);
    initializeModelMaterials(value);
    unmapped(value);
    value.materials[0].baseTexturePath = files.root / "Body.png";
    initializeModelMaterials(value);
    prepared(value, files.root / "Body_n.png");
}
void initializationRollback() {
    Temporary files;
    files.copy("Body.png");
    files.copy("Body_n.png");
    auto invalid = model(files.root);
    invalid.meshes.push_back(invalid.meshes[0]);
    invalid.meshes.back().indices[0] = 0xffffffffu;
    invalid.warnings = {"Retain original notice"};
    const auto original = invalid;
    bool threw = false;
    try {
        initializeModelMaterials(invalid);
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw && sameModel(invalid, original), "failed initialization rolls back the complete input model");
}
void actualPmxImports() {
    Temporary files;
    for (const auto* name : {"pmx-utf8.pmx", "pmx-utf16.pmx"}) {
        const auto directory = fs::path(name);
        const auto modelPath = files.copy(directory / "model.pmx", name);
        files.copy(directory / fs::path(u8"pmx-textures/\u5f69\u8272 pad%20.png"));
        const auto image = files.copy(directory / fs::path(u8"pmx-textures/\u5f69\u8272 pad%20N.png"));
        const auto value = loadModel(modelPath);
        const auto index = value.meshes.at(0).materialIndex;
        prepared(value, image, index);
        require(value.materials[index].baseTexturePath == image.parent_path() / fs::path(u8"\u5f69\u8272 pad%20.png"),
                "PMX base texture keeps its literal resolved Unicode/percent path");
        require(!value.materials.at(1).hasNormalMap,
                "unrelated PMX material does not inherit another material's normal map");
    }
}
void actualObjAndGltfImports() {
    Temporary files;
    files.text("obj/model.obj", "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\n"
                                "vn 0 0 1\nusemtl Cloth\nf 1/1/1 2/2/1 3/3/1\n");
    files.text("obj/model.mtl", "newmtl Cloth\nKd 1 1 1\nmap_Kd textures/Body_diffuse.png\n");
    const auto objBase = files.copy("obj/textures/Body_diffuse.png");
    const auto objImage = files.copy("obj/textures/Body_n.png");
    const auto obj = loadModel(files.root / "obj/model.obj");
    prepared(obj, objImage, obj.meshes.at(0).materialIndex);
    require(fs::remove(objBase), "remove only this test's owned temporary base image");
    for (const bool corrupt : {false, true}) {
        if (corrupt) files.text("obj/textures/Body_diffuse.png", "Deliberately invalid base image bytes.");
        const auto fallback = loadModel(files.root / "obj/model.obj");
        const auto index = fallback.meshes.at(0).materialIndex;
        prepared(fallback, objImage, index);
        require(!fallback.materials[index].hasTexture && fallback.materials[index].baseTexturePath == objBase,
                "missing or corrupt base image preserves its resolved path for sibling normal discovery");
        require(std::any_of(fallback.warnings.begin(), fallback.warnings.end(),
                            [](const auto& warning) {
                                return warning.find("Texture '") != std::string::npos &&
                                       warning.find("using white") != std::string::npos;
                            }),
                "unavailable base image still reports the existing white fallback warning");
    }
    const auto gltfPath = files.copy("gltf/model.gltf", "uri-spaces.gltf");
    files.copy("gltf/mesh data.bin", "mesh data.bin");
    files.copy("gltf/base color.png");
    const auto gltfImage = files.copy("gltf/base colorN.png");
    const auto gltf = loadModel(gltfPath);
    prepared(gltf, gltfImage, gltf.meshes.at(0).materialIndex);
    require(gltf.materials.at(gltf.meshes[0].materialIndex).baseTexturePath.filename() == "base color.png",
            "glTF base URI is decoded once before filename matching");
    std::ifstream input(fixture("normal-map.glb"), std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto key = bytes.find("normalTexture");
    require(key != std::string::npos, "original GLB contains the expected native normal declaration");
    bytes.replace(key, std::string_view("normalTexture").size(), "unusedTexture");
    files.text("glb/Hero.glb", bytes);
    const auto glbImage = files.copy("glb/Hero_n.png");
    const auto glb = loadModel(files.root / "glb/Hero.glb");
    prepared(glb, glbImage, glb.meshes.at(0).materialIndex);
}
void actualDeclaredNativeFallbacks() {
    Temporary files;
    std::size_t index = 0;
    for (const auto* fixtureName : {"normal-uv1.gltf", "normal-uv-transform.gltf", "normal-missing.gltf"}) {
        const auto directory = fs::path(std::to_string(index++));
        const auto source = files.copy(directory / "Scene.gltf", fixtureName);
        files.copy(directory / "Scene_n.png");
        files.copy(directory / "Original normal material_n.png");
        const auto value = loadModel(source);
        const auto materialIndex = value.meshes.at(0).materialIndex;
        unmapped(value, false, materialIndex);
        require(value.materials[materialIndex].normalTextureDeclared && !value.warnings.empty(),
                "actual native normal declaration remains authoritative even when unavailable or unsupported");
    }
    const auto source = files.copy("native/Native.gltf", "normal-map.gltf");
    files.copy("native/Native_n.png");
    const auto value = loadModel(source);
    const auto& native = value.materials.at(value.meshes.at(0).materialIndex);
    require(native.hasNormalMap && native.normalTextureDeclared && native.normalScale == 0.375f,
            "valid embedded native normal keeps its own map and scale");
}
}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"Unicode appended-N automatic normal", unicodeAppendedNormal},
        {"normal naming/color suffixes and case", commonNamesAndColorSuffixes},
        {"bounded dedicated normal directories", dedicatedNormalDirectories},
        {"automatic normal identity tier priority", identityTierPriority},
        {"material and single-used-material fallback", materialAndModelFallbacks},
        {"directory scope and unrelated images", boundedDirectoryScope},
        {"ambiguous automatic candidates", ambiguousCandidates},
        {"corrupt and missing automatic candidates", corruptAndMissingCandidates},
        {"native and manual normal precedence", nativeAndManualPrecedence},
        {"manual clear and override remain effective", manualClearAndOverride},
        {"source-less models do not scan cwd", sourceLessDoesNotScanCwd},
        {"automatic initialization transaction rollback", initializationRollback},
        {"actual PMX automatic normal import", actualPmxImports},
        {"actual OBJ/glTF/GLB automatic normal import", actualObjAndGltfImports},
        {"actual unusable/native normal precedence", actualDeclaredNativeFallbacks}};
    int failures = 0;
    for (const auto& [name, test] : tests) {
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
