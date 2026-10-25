#include <aeg/Model.hpp>
#include <aeg/OrbitCamera.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#ifndef AEG_SOURCE_DIR
#define AEG_SOURCE_DIR "."
#endif
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a - b) < 0.001f; }
bool finite(const glm::mat4& m) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(m[c][r])) return false;
    return true;
}
std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(AEG_SOURCE_DIR) / "tests" / "fixtures" / name;
}
void throws(const std::function<void()>& fn) {
    bool caught = false;
    try {
        fn();
    } catch (const std::exception&) {
        caught = true;
    }
    require(caught, "invalid input must throw");
}
void triangle() {
    const auto model = aeg::loadModel(fixture("triangle.obj"));
    require(model.vertexCount() == 3 && model.triangleCount() == 1, "triangle counts");
    require(model.meshes.size() == 1 && !model.materials.empty(), "mesh and default material");
    require(close(model.bounds.min.x, 0) && close(model.bounds.max.x, 2) && close(model.bounds.max.y, 3),
            "triangle bounds");
    for (const auto& v : model.meshes[0].vertices)
        require(close(glm::length(v.normal), 1) && v.normal.z > 0.99f, "generated normals");
    for (const auto i : model.meshes[0].indices) require(i < 3, "valid indices");
}
void transformed() {
    const auto model = aeg::loadModel(fixture("transformed.gltf"));
    require(model.triangleCount() == 1, "glTF triangle count");
    require(close(model.bounds.min.x, 7) && close(model.bounds.max.x, 11), "nested scale and translation");
    require(close(model.bounds.min.y, -2) && close(model.bounds.max.y, 1) && close(model.bounds.min.z, 7),
            "transformed world bounds");
}
void triangulation() {
    const auto model = aeg::loadModel(fixture("quad.obj"));
    require(model.triangleCount() == 2 && model.meshes.at(0).indices.size() == 6,
            "quad triangulates into two triangles");
    require(close(model.bounds.min.x, -1) && close(model.bounds.max.z, 1), "quad bounds");
}
void binaryAndEmbedded() {
    const auto binary = aeg::loadModel(fixture("transformed.glb"));
    require(binary.triangleCount() == 1 && close(binary.bounds.min.x, 7) && close(binary.bounds.max.x, 11),
            "GLB buffer and world transform");
    const auto embedded = aeg::loadModel(fixture("embedded.gltf"));
    const auto& material = embedded.materials.at(embedded.meshes.at(0).materialIndex);
    require(material.hasTexture && material.texture.width == 32 && material.texture.height == 32,
            "embedded PNG decode");
    require(close(material.baseColor.r, 0.3f) && close(material.baseColor.b, 0.7f), "glTF base color factor");
    require(material.texture.rgba[0] == 90 && material.texture.rgba[(31 * 32 + 1) * 4] == 205,
            "decoded image rows retain top-left order");
    bool foundOrigin = false, foundBottom = false;
    for (const auto& v : embedded.meshes.at(0).vertices) {
        if (close(v.position.x, 7) && close(v.position.y, -2)) {
            foundOrigin = true;
            require(close(v.uv.x, 0) && close(v.uv.y, 0), "glTF top-left UV origin survives Assimp import");
        }
        if (close(v.position.x, 7) && close(v.position.y, 1)) {
            foundBottom = true;
            require(close(v.uv.x, 0) && close(v.uv.y, 1), "glTF bottom UV maps to decoded final image row");
        }
    }
    require(foundOrigin && foundBottom, "UV fixture retains both referenced vertices");
}
void textureFallback() {
    const auto model = aeg::loadModel(fixture("missing-texture.obj"));
    const auto& material = model.materials.at(model.meshes.at(0).materialIndex);
    require(!model.warnings.empty(), "missing texture warning");
    require(!material.hasTexture && material.texture.width == 1 && material.texture.height == 1,
            "white texture fallback dimensions");
    require(material.texture.rgba == std::vector<std::uint8_t>({255, 255, 255, 255}), "white fallback pixels");
    require(close(material.baseColor.r, 0.25f) && close(material.baseColor.b, 0.75f), "material base color");
}
void alphaModes() {
    for (const auto* filename : {"alpha-opaque.gltf", "alpha-default.gltf"}) {
        const auto model = aeg::loadModel(fixture(filename));
        const auto& m = model.materials.at(model.meshes.at(0).materialIndex);
        require(!m.alphaMasked && close(m.baseColor.a, 0),
                "opaque mode preserves alpha factor without enabling discard");
        require(model.warnings.empty(), "opaque alpha is supported without warnings");
    }
    const auto mask = aeg::loadModel(fixture("alpha-mask.gltf"));
    const auto& masked = mask.materials.at(mask.meshes.at(0).materialIndex);
    require(masked.alphaMasked && close(masked.alphaCutoff, 0.5f) && close(masked.baseColor.a, 0.3f),
            "MASK uses default cutoff and preserves alpha factor");
    const auto custom = aeg::loadModel(fixture("alpha-mask-custom.gltf"));
    const auto& customized = custom.materials.at(custom.meshes.at(0).materialIndex);
    require(customized.alphaMasked && close(customized.alphaCutoff, 0.1f) && close(customized.baseColor.a, 0.3f),
            "MASK honors custom cutoff");
    const auto blend = aeg::loadModel(fixture("alpha-blend.gltf"));
    const auto& blended = blend.materials.at(blend.meshes.at(0).materialIndex);
    require(!blended.alphaMasked && close(blended.baseColor.a, 0.3f) && !blend.warnings.empty(),
            "BLEND warns and uses opaque fallback");
}
void externalUriSpaces() {
    const auto model = aeg::loadModel(fixture("uri-spaces.gltf"));
    const auto& m = model.materials.at(model.meshes.at(0).materialIndex);
    require(model.triangleCount() == 1 && m.hasTexture && m.texture.width == 32 && model.warnings.empty(),
            "percent-encoded spaces resolve both buffer and image");
}
void externalUriUnicode() {
    const auto model = aeg::loadModel(fixture("uri-unicode.gltf"));
    const auto& m = model.materials.at(model.meshes.at(0).materialIndex);
    require(model.triangleCount() == 1 && m.hasTexture && m.texture.width == 32 && model.warnings.empty(),
            "percent-encoded UTF-8 resolves while preserving plus signs");
}
void invalidResourceUris() {
    throws([] { (void)aeg::loadModel(fixture("uri-nul-buffer.gltf")); });
    const auto model = aeg::loadModel(fixture("uri-nul-image.gltf"));
    const auto& material = model.materials.at(model.meshes.at(0).materialIndex);
    require(!material.hasTexture && !model.warnings.empty(), "NUL in image URI uses warning and white fallback");
}
void literalPercentPaths() {
    auto temporary = std::filesystem::temp_directory_path() / "aeg-literal%20-plus+-";
    temporary += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{temporary};
    std::filesystem::create_directories(temporary);
    std::filesystem::copy_file(fixture("uri-spaces.gltf"), temporary / "scene%20.gltf");
    for (const auto* filename :
         {"mesh data.bin", "base color.png", "literal-percent.obj", "literal-percent.mtl", "literal%20texture.png"})
        std::filesystem::copy_file(fixture(filename), temporary / filename);
    const auto model = aeg::loadModel(temporary / "scene%20.gltf");
    require(model.triangleCount() == 1 && model.materials.at(model.meshes.at(0).materialIndex).hasTexture &&
                model.warnings.empty(),
            "literal percent in model filename and parent path remains literal");
    const auto obj = aeg::loadModel(temporary / "literal-percent.obj");
    require(obj.materials.at(obj.meshes.at(0).materialIndex).hasTexture && obj.warnings.empty(),
            "OBJ texture filenames retain literal percent escape text");
}
void bundled() {
    const auto model = aeg::loadModel(std::filesystem::path(AEG_SOURCE_DIR) / "assets/models/demo.obj");
    require(model.triangleCount() > 40 && model.meshes.size() > 1, "bundled multi-mesh demo");
    bool textured = false;
    for (const auto& m : model.materials)
        if (m.hasTexture) {
            textured = true;
            require(m.texture.width > 1 && m.texture.rgba.size() == m.texture.width * m.texture.height * 4,
                    "decoded texture pixels");
        }
    require(textured && model.warnings.empty(), "bundled texture and materials load");
}
void unicodePaths() {
    auto temporary = std::filesystem::temp_directory_path() / std::filesystem::path(u8"aeg-\u6a21\u578b with spaces-");
    temporary += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{temporary};
    std::filesystem::create_directories(temporary);
    for (const auto* filename : {"demo.obj", "demo.mtl", "pad.png"})
        std::filesystem::copy_file(std::filesystem::path(AEG_SOURCE_DIR) / "assets/models" / filename,
                                   temporary / filename);
    const auto model = aeg::loadModel(temporary / "demo.obj");
    bool textured = false;
    for (const auto& m : model.materials) textured |= m.hasTexture;
    require(model.triangleCount() > 40 && textured && model.warnings.empty(),
            "Unicode model, material and texture paths");
}
void pmxGeometry(const char* filename) {
    const auto model = aeg::loadModel(fixture(filename));
    require(model.vertexCount() == 24 && model.triangleCount() == 12, "PMX cube counts and sharp face seams");
    require(model.meshes.size() == 2 && model.materials.size() == 2, "PMX material groups remain distinct");
    require(close(model.bounds.min.x, -1) && close(model.bounds.max.x, 1) &&
                close(model.bounds.min.y, 0) && close(model.bounds.max.y, 2) &&
                close(model.bounds.min.z, -3) && close(model.bounds.max.z, -1),
            "PMX bounds convert from source left-handed coordinates");
    require(model.meshes[0].materialIndex != model.meshes[1].materialIndex, "PMX meshes use their own materials");
    bool foundTop = false, foundBottom = false;
    for (const auto& mesh : model.meshes) {
        require(mesh.vertices.size() == 12 && mesh.indices.size() == 18, "PMX material index ranges");
        require(mesh.materialIndex < model.materials.size(), "PMX mesh material index is valid");
        for (const auto index : mesh.indices) require(index < mesh.vertices.size(), "PMX triangle index is valid");
        for (std::size_t index = 0; index < mesh.indices.size(); index += 3) {
            const auto& a = mesh.vertices[mesh.indices[index]];
            const auto& b = mesh.vertices[mesh.indices[index + 1]];
            const auto& c = mesh.vertices[mesh.indices[index + 2]];
            require(glm::dot(glm::cross(b.position - a.position, c.position - a.position), a.normal) > 0,
                    "PMX triangle winding agrees with outward normals after handedness conversion");
        }
        for (const auto& vertex : mesh.vertices) {
            require(close(glm::length(vertex.normal), 1) &&
                        close(std::abs(vertex.normal.x) + std::abs(vertex.normal.y) +
                                  std::abs(vertex.normal.z), 1) &&
                        glm::dot(vertex.normal, vertex.position - glm::vec3(0, 1, -2)) > 0.99f,
                    "PMX supplied sharp normals remain normalized and point outward");
            if (vertex.normal.z < -0.99f && close(vertex.position.x, -1)) {
                if (close(vertex.position.y, 2)) {
                    foundTop = true;
                    require(close(vertex.uv.x, 0) && close(vertex.uv.y, 0), "PMX top vertex retains top-origin UV");
                } else if (close(vertex.position.y, 0)) {
                    foundBottom = true;
                    require(close(vertex.uv.x, 0) && close(vertex.uv.y, 1), "PMX bottom vertex retains bottom UV");
                }
            }
        }
    }
    require(foundTop && foundBottom, "PMX UV checks reference both source face corners");
    const auto& textured = model.materials.at(model.meshes[0].materialIndex);
    const auto& solid = model.materials.at(model.meshes[1].materialIndex);
    require(close(textured.baseColor.r, 0.25f) && close(textured.baseColor.g, 0.5f) &&
                close(textured.baseColor.b, 0.75f) && close(textured.baseColor.a, 1),
            "PMX textured diffuse color and opacity");
    require(close(solid.baseColor.r, 0.8f) && close(solid.baseColor.g, 0.2f) &&
                close(solid.baseColor.b, 0.1f) && close(solid.baseColor.a, 1) && !solid.hasTexture,
            "PMX untextured material preserves its diffuse color");
    require(textured.hasTexture && textured.texture.width == 32 && textured.texture.height == 32 &&
                textured.texture.rgba.size() == 32 * 32 * 4,
            "PMX Unicode texture path with backslash, space and literal percent resolves");
    require(textured.texture.rgba[0] == 90 && textured.texture.rgba[1] == 115 &&
                textured.texture.rgba[(31 * 32 + 1) * 4] == 205,
            "PMX texture decode preserves top-left image row order");
}
void pmxUnicodePaths() {
    auto temporary = std::filesystem::temp_directory_path() /
                     std::filesystem::path(u8"aeg-pmx-\u6a21\u578b %20 with spaces-");
    temporary += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{temporary};
    std::filesystem::create_directories(temporary);
    const auto source = temporary / std::filesystem::path(u8"\u539f\u521b cube%20.pmx");
    std::filesystem::copy_file(fixture("pmx-utf16.pmx"), source);
    std::filesystem::copy(fixture("pmx-textures"), temporary / "pmx-textures",
                          std::filesystem::copy_options::recursive);
    const auto model = aeg::loadModel(source);
    require(model.source == source && model.triangleCount() == 12 &&
                model.materials.at(model.meshes.at(0).materialIndex).hasTexture,
            "PMX Unicode source filename and parent preserve literal material texture reference");
}
void pmxTextureFallback() {
    const auto model = aeg::loadModel(fixture("pmx-missing-texture.pmx"));
    const auto& material = model.materials.at(model.meshes.at(0).materialIndex);
    require(model.triangleCount() == 12, "missing PMX texture does not prevent geometry import");
    require(!material.hasTexture && material.texture.width == 1 && material.texture.height == 1 &&
                material.texture.rgba == std::vector<std::uint8_t>({255, 255, 255, 255}),
            "missing PMX texture uses white fallback");
    require(close(material.baseColor.r, 0.25f) && close(material.baseColor.b, 0.75f),
            "missing PMX texture retains diffuse color");
    bool textureWarning = false;
    for (const auto& warning : model.warnings)
        textureWarning |= warning.find("absent.png") != std::string::npos;
    require(textureWarning, "missing PMX texture warning identifies unavailable resource");
}
void pmxOpacity() {
    const auto model = aeg::loadModel(fixture("pmx-alpha.pmx"));
    const auto& partial = model.materials.at(model.meshes.at(0).materialIndex);
    const auto& hidden = model.materials.at(model.meshes.at(1).materialIndex);
    require(close(partial.baseColor.a, 0.4f) && close(hidden.baseColor.a, 0),
            "PMX diffuse opacity is retained, including zero");
    require(partial.alphaMasked && hidden.alphaMasked && close(partial.alphaCutoff, 0.1f) &&
                close(hidden.alphaCutoff, 0.1f),
            "PMX cutout metadata clips transparent texels and zero-opacity materials");
    require(partial.hasTexture && partial.texture.width == 2 && partial.texture.height == 2 &&
                partial.texture.rgba[3] == 0 && partial.texture.rgba[7] == 255 &&
                partial.texture.rgba[11] == 255 && partial.texture.rgba[15] == 0,
            "PMX cutout image alpha survives texture decode");
    bool opacityWarning = false;
    for (const auto& warning : model.warnings)
        opacityWarning |= warning.find("partial") != std::string::npos || warning.find("transluc") != std::string::npos;
    require(opacityWarning, "PMX partial opacity warns that alpha blending is approximated");
}
void pmxSignatureDetection() {
    auto temporary = std::filesystem::temp_directory_path() / "aeg-pmx-signature-";
    temporary += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{temporary};
    std::filesystem::create_directories(temporary);
    std::filesystem::copy(fixture("pmx-textures"), temporary / "pmx-textures",
                          std::filesystem::copy_options::recursive);
    const auto renamed = temporary / "model.bin";
    std::filesystem::copy_file(fixture("pmx-alpha.pmx"), renamed);
    const auto model = aeg::loadModel(renamed);
    const auto& material = model.materials.at(model.meshes.at(0).materialIndex);
    require(model.triangleCount() == 12 && material.alphaMasked && close(material.baseColor.a, 0.4f),
            "signature-detected PMX uses PMX material rules regardless of extension");
    const auto invalid = temporary / "invalid.bin";
    std::filesystem::copy_file(fixture("pmx-bad-uv-header.pmx"), invalid);
    throws([&] { (void)aeg::loadModel(invalid); });
}
void pmxInvalidReference(const char* filename, const char* category) {
    try {
        (void)aeg::loadModel(fixture(filename));
    } catch (const std::exception& error) {
        auto message = std::string(error.what());
        // A descriptive fixture filename must not satisfy the category check.
        for (auto offset = message.find(filename); offset != std::string::npos; offset = message.find(filename))
            message.erase(offset, std::string(filename).size());
        const auto separator = message.find(": ");
        auto diagnostic = separator == std::string::npos ? message : message.substr(separator + 2);
        for (auto& character : diagnostic)
            if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
        if (diagnostic.find(category) == std::string::npos)
            throw std::runtime_error("PMX rejection must identify " + std::string(category) + ": " + error.what());
        return;
    }
    throw std::runtime_error("Invalid PMX reference must throw a standard exception");
}
void horizontalOrbitFollowsDrag() {
    aeg::Bounds bounds;
    bounds.min = {-1, -1, -1};
    bounds.max = {1, 1, 1};
    for (const float initialDrag : {-900.f, 0.f, 900.f}) {
        for (const float direction : {-1.f, 1.f}) {
            aeg::OrbitCamera camera;
            camera.fit(bounds);
            camera.orbit(initialDrag, 0);
            const auto initialEye = camera.position();
            const auto marker = camera.target() + glm::normalize(initialEye - camera.target()) * 0.5f;
            const auto screenX = [&] {
                const auto clip = camera.projection(1.6f) * camera.view() * glm::vec4(marker, 1);
                require(clip.w > 0, "front-surface marker remains visible during orbit");
                return clip.x / clip.w;
            };
            const float before = screenX();
            camera.orbit(direction * 80, 0);
            require(direction * (screenX() - before) > 0.01f,
                    "visible model surface follows horizontal mouse drag");
            require(close(camera.position().y, initialEye.y), "horizontal drag preserves camera elevation");
        }
    }
}
void camera() {
    aeg::OrbitCamera c;
    aeg::Bounds b;
    b.min = {-2, -1, -3};
    b.max = {2, 1, 3};
    c.fit(b);
    require(c.distance() > b.radius(), "fit keeps eye outside model");
    for (float aspect : {0.2f, 1.0f, 3.0f}) {
        const auto projection = c.projection(aspect);
        require(finite(projection) && projection[1][1] < 0, "Vulkan finite Y-flipped projection");
        const auto vp = projection * c.view();
        for (int i = 0; i < 8; ++i) {
            glm::vec3 p{(i & 1) ? 2.f : -2.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 3.f : -3.f};
            const auto clip = vp * glm::vec4(p, 1);
            require(clip.w > 0 && std::abs(clip.x / clip.w) < 1 && std::abs(clip.y / clip.w) < 1 &&
                        clip.z / clip.w > 0 && clip.z / clip.w < 1,
                    "fit contains all corners across aspect ratios");
        }
    }
    c.orbit(1e8f, 1e8f);
    require(finite(c.view()), "orbit pitch clamps");
    c.zoom(1e8f);
    require(c.distance() > 0 && finite(c.view()), "zoom minimum finite");
    c.zoom(-1e8f);
    require(c.distance() < 1e8f && finite(c.projection(1)), "zoom maximum finite");
    c.pan(20, 10, 600);
    require(finite(c.view()), "pan remains finite");
    c.orbit(std::numeric_limits<float>::infinity(), 0);
    c.zoom(std::numeric_limits<float>::quiet_NaN());
    require(finite(c.view()), "ignore invalid input");
    require(finite(c.projection(0)), "zero viewport aspect safe");
}
}
int main() {
    int failures = 0;
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"triangle import", triangle},
        {"quad triangulation", triangulation},
        {"transformed glTF", transformed},
        {"missing model", [] { throws([] { (void)aeg::loadModel(fixture("absent.obj")); }); }},
        {"no triangles", [] { throws([] { (void)aeg::loadModel(fixture("empty.obj")); }); }},
        {"non-finite geometry", [] { throws([] { (void)aeg::loadModel(fixture("nonfinite.gltf")); }); }},
        {"GLB and embedded texture", binaryAndEmbedded},
        {"texture fallback", textureFallback},
        {"alpha modes", alphaModes},
        {"external URI spaces", externalUriSpaces},
        {"external URI Unicode", externalUriUnicode},
        {"invalid resource URIs", invalidResourceUris},
        {"literal percent paths", literalPercentPaths},
        {"bundled demo", bundled},
        {"Unicode paths", unicodePaths},
        {"PMX UTF-8 geometry and materials", [] { pmxGeometry("pmx-utf8.pmx"); }},
        {"PMX UTF-16 geometry and materials", [] { pmxGeometry("pmx-utf16.pmx"); }},
        {"PMX four additional UV channels", [] { pmxGeometry("pmx-extra-uv.pmx"); }},
        {"PMX Unicode paths", pmxUnicodePaths},
        {"PMX texture fallback", pmxTextureFallback},
        {"PMX opacity and cutout", pmxOpacity},
        {"PMX signature detection and header guard", pmxSignatureDetection},
        {"PMX truncated input", [] { throws([] { (void)aeg::loadModel(fixture("pmx-truncated.pmx")); }); }},
        {"PMX rejects excessive UV channels", [] {
             throws([] { (void)aeg::loadModel(fixture("pmx-bad-uv-header.pmx")); });
         }},
        {"PMX rejects invalid string encoding", [] {
             throws([] { (void)aeg::loadModel(fixture("pmx-bad-encoding.pmx")); });
         }},
        {"PMX rejects invalid index width", [] {
             throws([] { (void)aeg::loadModel(fixture("pmx-bad-index-width.pmx")); });
         }},
        {"PMX rejects out-of-range vertex index", [] {
             pmxInvalidReference("pmx-bad-vertex-index.pmx", "vertex");
         }},
        {"PMX rejects material span beyond indices", [] {
             pmxInvalidReference("pmx-bad-material-span.pmx", "material");
         }},
        {"PMX rejects out-of-range texture index", [] {
             pmxInvalidReference("pmx-bad-texture.pmx", "texture");
         }},
        {"PMX rejects cyclic bone parent", [] {
             pmxInvalidReference("pmx-bad-bone-parent.pmx", "bone");
         }},
        {"PMX invalid skinning reports standard exception", [] {
             throws([] { (void)aeg::loadModel(fixture("pmx-bad-skinning.pmx")); });
         }},
        {"orbit camera", camera},
        {"horizontal orbit follows drag", horizontalOrbitFollowsDrag}};
    for (const auto& [name, test] : tests) try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    return failures == 0 ? 0 : 1;
}
