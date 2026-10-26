#include <ModelData.hpp>
#include <ModelImporter.hpp>
#include <MaterialTextures.hpp>
#include <Utf8Path.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef AEG_SOURCE_DIR
#define AEG_SOURCE_DIR "."
#endif

namespace {
using aeg::assets::ModelData;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a - b) < 0.001f; }
ModelData load(const char* name) {
    return aeg::assets::loadModel(std::filesystem::path(AEG_SOURCE_DIR) / "tests/fixtures" / name);
}
const aeg::assets::MaterialData& material(const ModelData& model) {
    return model.materials.at(model.meshes.at(0).materialIndex);
}
std::size_t warnings(const ModelData& model, std::string_view needle) {
    std::size_t count = 0;
    for (const auto& warning : model.warnings)
        if (warning.find(needle) != std::string::npos) ++count;
    return count;
}
void fallback(const ModelData& model) {
    const auto& m = material(model);
    require(!m.hasNormalMap, "unsupported normal texture uses vertex-normal shading");
    require(m.normalTexture.width == 1 && m.normalTexture.height == 1 &&
                m.normalTexture.rgba == std::vector<std::uint8_t>({128, 128, 255, 255}),
            "normal fallback is a neutral linear pixel");
}
void frames(const ModelData& model, bool usable = true) {
    for (const auto& mesh : model.meshes)
        for (const auto& v : mesh.vertices) {
            require(std::isfinite(v.tangent.x) && std::isfinite(v.tangent.y) && std::isfinite(v.tangent.z) &&
                        std::isfinite(v.tangent.w),
                    "tangent frame must remain finite");
            if (!usable) {
                require(v.tangent.w == 0, "unusable UV basis requests geometric-normal fallback");
                continue;
            }
            require(close(aeg::math::length(aeg::math::Vec3(v.tangent)), 1) &&
                        std::abs(aeg::math::dot(v.normal, aeg::math::Vec3(v.tangent))) < 0.001f &&
                        std::abs(v.tangent.w) == 1,
                    "usable tangent is unit, perpendicular to the normal and signed");
        }
}
void importedImages() {
    for (const auto* name : {"normal-map.gltf", "normal-map.glb", "normal-map.obj", "normal-map-kn.obj"}) {
        const auto model = load(name);
        const auto& m = material(model);
        require(m.hasNormalMap && !m.hasTexture, "normal image imports independently of base color");
        require(m.normalTexture.width == 2 && m.normalTexture.height == 2, "normal image dimensions survive import");
        require(m.normalTexture.rgba == std::vector<std::uint8_t>({128, 128, 255, 255, 220, 128, 210, 255, 128, 220,
                                                                   210, 255, 32, 96, 220, 255}),
                "normal image bytes retain top-left rows without gamma conversion");
        require(close(m.normalScale,
                      name == std::string_view("normal-map.gltf") || name == std::string_view("normal-map.glb") ? 0.375f
                                                                                                                : 1.0f),
                "glTF scale is imported and OBJ defaults to one");
        require(model.warnings.empty(), "supported normal map imports without warnings");
        frames(model);
    }
}
void normalScales() {
    const auto zero = load("normal-scale-zero.gltf");
    const auto negative = load("normal-scale-negative.gltf");
    require(material(zero).hasNormalMap && material(zero).normalScale == 0,
            "zero normal scale is a valid material value");
    require(material(negative).hasNormalMap && close(material(negative).normalScale, -1.5f),
            "negative finite normal scale is preserved");
}
void materialFallbacks() {
    const auto missing = load("normal-missing.gltf");
    fallback(missing);
    require(warnings(missing, "Normal texture") == 1 && warnings(missing, "unavailable") == 1,
            "missing normal image reports its fallback once");
    const auto uv1 = load("normal-uv1.gltf");
    fallback(uv1);
    require(warnings(uv1, "UV0") == 1, "unsupported normal coordinate set is reported");
    const auto transformed = load("normal-uv-transform.gltf");
    fallback(transformed);
    require(warnings(transformed, "transform") == 1, "unsupported normal UV transform is reported");
    const auto transformedObj = load("normal-obj-transform.obj");
    fallback(transformedObj);
    require(warnings(transformedObj, "transform") == 1,
            "OBJ normal UV transform discarded by Assimp is explicitly reported");
    const auto height = load("normal-height.obj");
    fallback(height);
    require(warnings(height, "height") == 1, "OBJ bump map is explicitly treated as height, not RGB normals");
    const auto none = load("triangle.obj");
    fallback(none);
    require(none.vertexCount() == 3 && none.triangleCount() == 1 && none.warnings.empty(),
            "legacy geometry retains counts and quiet vertex-normal lighting");
}
void ordinaryBasis() {
    const auto model = load("normal-map.gltf");
    frames(model);
    for (const auto& v : model.meshes.at(0).vertices)
        require(v.tangent.x > 0.999f && close(v.tangent.y, 0) && close(v.tangent.z, 0) && v.tangent.w == 1,
                "UV0 +U and +V produce the authored +X/+Y basis");
}
void nearlyParallelBasis() {
    const auto model = load("normal-nearly-parallel.gltf");
    frames(model);
    for (const auto& vertex : model.meshes.at(0).vertices)
        require(vertex.tangent.z > 0.999f,
                "nearly normal-parallel +U retains its small orthogonal direction without float-normal residue");
}
void mirroredBasis() {
    const auto model = load("normal-mirrored-uv.gltf");
    frames(model);
    require(model.triangleCount() == 2 && model.vertexCount() == 6,
            "shared vertices split at opposite tangent handedness");
    const auto& mesh = model.meshes.at(0);
    for (std::size_t corner = 0; corner < 6; ++corner)
        require(mesh.vertices.at(mesh.indices.at(corner)).tangent.w == (corner < 3 ? 1 : -1),
                "every mirrored UV face corner retains its correct handedness");
    require(model.warnings.empty(), "mirrored but usable UVs are supported");
}
void invalidBasis() {
    const auto degenerate = load("normal-degenerate-uv.gltf");
    frames(degenerate, false);
    require(material(degenerate).hasNormalMap && warnings(degenerate, "degenerate") == 1,
            "degenerate UV basis keeps the texture and warns once for its mesh");
    const auto missing = load("normal-no-uv.gltf");
    frames(missing, false);
    require(material(missing).hasNormalMap && warnings(missing, "UV0") == 1,
            "missing UV0 falls back per mesh with one warning");
    const auto mixed = load("normal-mixed-uv.gltf");
    const auto& mesh = mixed.meshes.at(0);
    require(
        mesh.vertices.at(mesh.indices.at(0)).tangent.w != 0 && mesh.vertices.at(mesh.indices.at(3)).tangent.w == 0 &&
            mesh.vertices.at(mesh.indices.at(4)).tangent.w == 0 && mesh.vertices.at(mesh.indices.at(5)).tangent.w == 0,
        "degenerate face does not borrow a shared valid face basis");
    require(warnings(mixed, "degenerate") == 1, "mixed UV mesh warning is bounded");
}
void transformedBasis() {
    for (const auto* name : {"normal-nonuniform.gltf", "normal-mirrored-node.gltf"}) {
        const auto model = load(name);
        frames(model);
        const bool mirrored = name == std::string_view("normal-mirrored-node.gltf");
        for (const auto& v : model.meshes.at(0).vertices) {
            // Source +U=(1,1,0); node linear scale=(+/-2,1,0.5).
            require(close(v.tangent.x, mirrored ? -0.89442719f : 0.89442719f) && close(v.tangent.y, 0.44721360f) &&
                        close(v.tangent.z, 0),
                    "tangent follows final positions under nonuniform node transforms");
            require(v.tangent.w == (mirrored ? -1 : 1), "mirrored node determinant changes the final UV basis sign");
            require(close(v.normal.x, mirrored ? -0.12038585f : 0.12038585f) && close(v.normal.y, -0.24077170f) &&
                        close(v.normal.z, 0.96308682f),
                    "normal keeps the existing normalized inverse-transpose node transform");
        }
    }
}

void formatIndependentAssignment() {
    for (const auto* name :
         {"pmx-utf8.pmx", "pmx-utf16.pmx", "normal-height.obj", "normal-map.gltf", "normal-map.glb"}) {
        auto model = load(name);
        const auto index = model.meshes.at(0).materialIndex;
        const auto triangles = model.triangleCount();
        aeg::assets::setMaterialNormalMap(model, index, aeg::assets::ImageData{1, 1, {204, 128, 230, 255}}, -0.5f);
        require(model.materials.at(index).hasNormalMap && model.materials.at(index).normalScale == -0.5f,
                "every enabled format accepts the same normal material API");
        require(model.triangleCount() == triangles, "normal assignment preserves triangles");
        for (const auto& mesh : model.meshes)
            if (mesh.materialIndex == index)
                for (const auto& vertex : mesh.vertices)
                    require(std::abs(vertex.tangent.w) == 1, "assigned material receives generated tangent frames");
        aeg::assets::clearMaterialNormalMap(model, index);
        require(!model.materials.at(index).hasNormalMap && model.materials.at(index).normalScale == 1,
                "every enabled format clears its normal map through the common API");
    }
    ModelData procedural;
    procedural.name = "Procedural model without an extension";
    auto& mesh = procedural.meshes.emplace_back();
    mesh.vertices = {{{0, 0, 0}, {0, 0, 1}, {0, 0}}, {{1, 0, 0}, {0, 0, 1}, {1, 0}}, {{0, 1, 0}, {0, 0, 1}, {0, 1}}};
    mesh.indices = {0, 1, 2};
    auto& normal = procedural.materials.emplace_back();
    normal.hasNormalMap = true;
    normal.normalTexture = {1, 1, {204, 128, 230, 255}};
    aeg::assets::prepareModelMaterials(procedural);
    frames(procedural);
}

bool sameFloat(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }
template <typename Vector> bool sameVector(const Vector& a, const Vector& b) {
    for (std::size_t i = 0; i < Vector::component_count; ++i)
        if (!sameFloat(a[i], b[i])) return false;
    return true;
}
void semanticVectorComparison() {
    aeg::math::Vec3 a{1, 2, 3}, b{1, 2, 3};
    const float poison = std::numeric_limits<float>::quiet_NaN();
    // Vec3's fourth lane is object padding, never a geometry component.
    std::memcpy(reinterpret_cast<unsigned char*>(&a) + 3 * sizeof(float), &poison, sizeof(poison));
    const float otherPadding = -100;
    std::memcpy(reinterpret_cast<unsigned char*>(&b) + 3 * sizeof(float), &otherPadding, sizeof(otherPadding));
    require(sameVector(a, b), "transaction comparisons ignore Vec3 padding");
    require(a == b && aeg::math::is_finite(a) && close(aeg::math::length(a), std::sqrt(14.0f)),
            "geometry equality, finiteness and length ignore Vec3 padding");
    b.z = 4;
    require(!sameVector(a, b), "transaction comparisons include every semantic Vec3 component");
}
bool sameImage(const aeg::assets::ImageData& a, const aeg::assets::ImageData& b) {
    return a.width == b.width && a.height == b.height && a.rgba == b.rgba;
}
bool sameMaterial(const aeg::assets::MaterialData& a, const aeg::assets::MaterialData& b) {
    return a.name == b.name && sameVector(a.baseColor, b.baseColor) && sameImage(a.texture, b.texture) &&
           a.hasTexture == b.hasTexture && a.alphaMode == b.alphaMode && sameFloat(a.alphaCutoff, b.alphaCutoff) &&
           sameImage(a.normalTexture, b.normalTexture) && a.hasNormalMap == b.hasNormalMap &&
           sameFloat(a.normalScale, b.normalScale) && a.normalTextureSource == b.normalTextureSource &&
           sameFloat(a.metallicFactor, b.metallicFactor) && sameFloat(a.roughnessFactor, b.roughnessFactor);
}
bool sameMesh(const aeg::assets::MeshData& a, const aeg::assets::MeshData& b) {
    if (a.name != b.name || a.indices != b.indices || a.materialIndex != b.materialIndex || a.hasUV0 != b.hasUV0 ||
        a.vertices.size() != b.vertices.size())
        return false;
    for (std::size_t i = 0; i < a.vertices.size(); ++i) {
        const auto& x = a.vertices[i];
        const auto& y = b.vertices[i];
        if (!sameVector(x.position, y.position) || !sameVector(x.normal, y.normal) || !sameVector(x.uv, y.uv) ||
            !sameVector(x.tangent, y.tangent))
            return false;
    }
    return true;
}
bool sameModel(const ModelData& a, const ModelData& b) {
    if (a.name != b.name || a.source != b.source || a.warnings != b.warnings ||
        !sameVector(a.bounds.min, b.bounds.min) || !sameVector(a.bounds.max, b.bounds.max) ||
        a.meshes.size() != b.meshes.size() || a.materials.size() != b.materials.size())
        return false;
    for (std::size_t i = 0; i < a.materials.size(); ++i)
        if (!sameMaterial(a.materials[i], b.materials[i])) return false;
    for (std::size_t i = 0; i < a.meshes.size(); ++i)
        if (!sameMesh(a.meshes[i], b.meshes[i])) return false;
    return true;
}
void failurePreserves(ModelData model, const std::function<void(ModelData&)>& operation) {
    const auto before = model;
    bool rejected = false;
    try {
        operation(model);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "invalid normal operation must report failure");
    require(sameModel(model, before), "failed normal operation preserves the complete CPU model");
}
void assignmentIsolation() {
    auto model = load("pmx-utf8.pmx");
    const auto before = model;
    const auto index = model.meshes.at(0).materialIndex;
    aeg::assets::setMaterialNormalMap(model, index, aeg::assets::ImageData{1, 1, {204, 128, 230, 255}});
    require(model.materials.size() > 1, "isolation fixture has multiple materials");
    for (std::size_t i = 0; i < model.materials.size(); ++i)
        if (i != index)
            require(sameMaterial(model.materials[i], before.materials[i]),
                    "normal assignment preserves other materials");
    for (std::size_t i = 0; i < model.meshes.size(); ++i)
        if (model.meshes[i].materialIndex != index)
            require(sameMesh(model.meshes[i], before.meshes[i]), "normal assignment preserves unrelated meshes");
    require(sameImage(model.materials[index].texture, before.materials[index].texture) &&
                sameVector(model.materials[index].baseColor, before.materials[index].baseColor) &&
                model.materials[index].alphaMode == before.materials[index].alphaMode,
            "normal assignment preserves diffuse and alpha material policy");
    auto seam = load("normal-mirrored-uv.gltf");
    const auto seamIndex = seam.meshes[0].materialIndex;
    const auto vertices = seam.vertexCount();
    for (int repetition = 0; repetition < 4; ++repetition) {
        aeg::assets::setMaterialNormalMap(seam, seamIndex, aeg::assets::ImageData{1, 1, {128, 204, 230, 255}}, 0);
        aeg::assets::prepareModelMaterials(seam);
        require(seam.vertexCount() == vertices, "replacing normal maps does not repeatedly split existing UV seams");
        frames(seam);
    }
    const auto geometry = seam.meshes;
    aeg::assets::clearMaterialNormalMap(seam, seamIndex);
    const auto& cleared = seam.materials[seamIndex];
    require(!cleared.hasNormalMap && cleared.normalScale == 1 && cleared.normalTextureSource.empty() &&
                cleared.normalTexture.rgba == std::vector<std::uint8_t>({128, 128, 255, 255}),
            "clear resets the normal material to its neutral state");
    for (std::size_t i = 0; i < seam.meshes.size(); ++i)
        require(sameMesh(seam.meshes[i], geometry[i]), "clear retains the prepared mesh topology");
}
void normalFilePaths() {
    auto model = load("pmx-utf8.pmx");
    const auto index = model.meshes[0].materialIndex;
    aeg::assets::setMaterialNormalMap(model, index, std::filesystem::path("normal-map.png"), 0.25f);
    require(model.materials[index].normalTexture.width == 2 && model.materials[index].normalScale == 0.25f,
            "relative normal image resolves beside a PMX model");
    const auto normalPath = std::filesystem::path(AEG_SOURCE_DIR) / "tests/fixtures/normal-map.png";
    const auto temporaryRoot = std::filesystem::temp_directory_path();
    std::random_device random;
    std::filesystem::path directory;
    bool created = false;
    for (int attempt = 0; attempt < 16 && !created; ++attempt) {
        directory = temporaryRoot /
                    ("aeg-normal-files-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     "-" + std::to_string(random()));
        created = std::filesystem::create_directory(directory);
    }
    require(created, "normal file path test owns a unique temporary directory");
    const auto unicodePath = directory / std::filesystem::path(u8"\u6cd5\u7ebf %20+ with spaces.png");
    struct Cleanup {
        std::filesystem::path file, directory;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(file, error);
            std::filesystem::remove(directory, error);
        }
    } cleanup{unicodePath, directory};
    std::filesystem::copy_file(normalPath, unicodePath);
    aeg::assets::setMaterialNormalMap(model, index, unicodePath);
    require(model.materials[index].normalTextureSource == aeg::foundation::pathToUtf8(unicodePath.lexically_normal()),
            "source preserves Unicode, spaces, plus and literal percent in an absolute image path");
    const auto expected = model.materials[index].normalTexture;
    model.source = directory / std::filesystem::path(u8"\u6a21\u578b.pmx");
    aeg::assets::setMaterialNormalMap(model, index, unicodePath.filename());
    require(sameImage(model.materials[index].normalTexture, expected),
            "relative images use literal filesystem names instead of URI percent decoding");
    model.source.clear();
    const auto fromCwd = std::filesystem::relative(normalPath, std::filesystem::current_path());
    aeg::assets::setMaterialNormalMap(model, index, fromCwd);
    require(sameImage(model.materials[index].normalTexture, expected),
            "source-free models resolve relative images from the current directory");
    aeg::assets::setMaterialNormalMap(model, index, aeg::assets::ImageData{});
    require(model.materials[index].normalTextureSource.empty(),
            "memory image assignment clears a previous file source");
}
void commonBasisFallbacks() {
    for (const auto* name : {"normal-no-uv.gltf", "normal-degenerate-uv.gltf"}) {
        auto model = load(name);
        const auto index = model.meshes[0].materialIndex;
        const auto noticeCount = model.warnings.size();
        aeg::assets::setMaterialNormalMap(model, index, aeg::assets::ImageData{});
        aeg::assets::prepareModelMaterials(model);
        frames(model, false);
        require(model.warnings.size() == noticeCount, "repeated common preparation deduplicates fallback notices");
        require(model.meshes[0].hasUV0 == (name == std::string_view("normal-degenerate-uv.gltf")),
                "import records whether UV0 actually exists");
    }
}
void assignmentFailures() {
    auto model = load("normal-map.gltf");
    const auto index = model.meshes[0].materialIndex;
    const auto invalid = model.materials.size();
    failurePreserves(model,
                     [&](auto& value) { aeg::assets::setMaterialNormalMap(value, invalid, aeg::assets::ImageData{}); });
    failurePreserves(model, [&](auto& value) { aeg::assets::clearMaterialNormalMap(value, invalid); });
    for (const auto& image :
         {aeg::assets::ImageData{0, 1, {}}, aeg::assets::ImageData{1, 0, {}},
          aeg::assets::ImageData{2, 1, {128, 128, 255, 255}}, aeg::assets::ImageData{0xffffffffu, 0xffffffffu, {}}})
        failurePreserves(model, [&](auto& value) { aeg::assets::setMaterialNormalMap(value, index, image); });
    for (const auto scale : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
        failurePreserves(model, [&](auto& value) {
            aeg::assets::setMaterialNormalMap(value, index, aeg::assets::ImageData{}, scale);
        });
    for (const auto* name : {"", "normal-no-such-file.png", "triangle.obj"})
        failurePreserves(
            model, [&](auto& value) { aeg::assets::setMaterialNormalMap(value, index, std::filesystem::path(name)); });
    for (int failure = 0; failure < 8; ++failure) {
        auto invalidModel = model;
        invalidModel.meshes.push_back(invalidModel.meshes[0]);
        auto& mesh = invalidModel.meshes.back();
        if (failure == 0) mesh.indices[0] = 0xffffffffu;
        if (failure == 1) mesh.indices.pop_back();
        if (failure == 2) mesh.materialIndex = 0xffffffffu;
        if (failure == 3) mesh.vertices[0].position.x = std::numeric_limits<float>::infinity();
        if (failure == 4) mesh.vertices[0].normal = aeg::math::Vec3(0);
        if (failure == 5) mesh.vertices[0].uv.x = std::numeric_limits<float>::quiet_NaN();
        if (failure == 6) mesh.vertices.clear();
        if (failure == 7) mesh.indices.clear();
        failurePreserves(invalidModel, [&](auto& value) {
            aeg::assets::setMaterialNormalMap(value, index, aeg::assets::ImageData{});
        });
        failurePreserves(invalidModel, [](auto& value) { aeg::assets::prepareModelMaterials(value); });
    }
    model.materials[index].normalTexture.rgba.clear();
    failurePreserves(model, [](auto& value) { aeg::assets::prepareModelMaterials(value); });
}
}

int main() {
    int failures = 0;
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"semantic vector rollback comparisons", semanticVectorComparison},
        {"normal image and scale imports", importedImages},
        {"finite normal scales", normalScales},
        {"normal material fallbacks", materialFallbacks},
        {"ordinary generated tangent basis", ordinaryBasis},
        {"nearly normal-parallel tangent projection", nearlyParallelBasis},
        {"mirrored UV tangent seam", mirroredBasis},
        {"degenerate and missing UV bases", invalidBasis},
        {"nonuniform and mirrored node bases", transformedBasis},
        {"format independent normal material assignment", formatIndependentAssignment},
        {"material assignment isolation and stable seams", assignmentIsolation},
        {"normal file path contracts", normalFilePaths},
        {"common material basis fallbacks", commonBasisFallbacks},
        {"normal assignment validation and transactions", assignmentFailures}};
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
