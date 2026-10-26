#include <ModelImporter.hpp>
#include <MaterialTextures.hpp>
#include <Utf8Path.hpp>
#include "ResourcePath.hpp"
#include "assimp/AssimpFileSystem.hpp"
#include "image/ImageDecoder.hpp"
#include "mmd/MmdEffectNormals.hpp"
#include "pmx/PmxValidation.hpp"
#include <assimp/Importer.hpp>
#include <assimp/BaseImporter.h>
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <unordered_set>

namespace aeg::assets {
namespace {
bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool finite(const glm::vec4& value) { return finite(glm::vec3(value)) && std::isfinite(value.w); }

std::unordered_set<std::string> transformedObjNormals(const std::vector<std::filesystem::path>& openedFiles) {
    std::unordered_set<std::string> transformed;
    for (const auto& filename : openedFiles) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) continue;
        std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (bytes.size() < 8) continue;
        // Match the text/BOM normalization used by Assimp's material parser.
        Assimp::BaseImporter::ConvertToUTF8(bytes);
        std::istringstream lines(std::string(bytes.begin(), bytes.end()));
        std::string line, materialName;
        while (std::getline(lines, line)) {
            std::istringstream fields(line);
            std::string directive;
            if (!(fields >> directive)) continue;
            std::transform(directive.begin(), directive.end(), directive.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (directive == "newmtl") {
                std::getline(fields, materialName);
                const auto first = materialName.find_first_not_of(" \t\r");
                materialName = first == std::string::npos
                                   ? AI_DEFAULT_MATERIAL_NAME
                                   : materialName.substr(first, materialName.find_last_not_of(" \t\r") - first + 1);
            } else if ((directive == "norm" || directive == "map_kn") && !materialName.empty()) {
                bool hasTransform = false;
                std::string option;
                while (fields >> option && option.starts_with('-')) {
                    std::transform(option.begin(), option.end(), option.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (option == "-o" || option == "-s" || option == "-t") {
                        hasTransform = true;
                        break;
                    }
                    // Assimp 5.4.3 discards these options before creating
                    // aiMaterial. Skip their arguments identically so filenames
                    // and unrelated scalar options are not treated as transforms.
                    unsigned arguments = option == "-mm" ? 2 : 0;
                    if (option == "-clamp" || option == "-type" || option == "-bm" || option == "-blendu" ||
                        option == "-blendv" || option == "-boost" || option == "-texres" || option == "-imfchan")
                        arguments = 1;
                    for (unsigned argument = 0; argument < arguments && fields >> option; ++argument) {
                    }
                }
                if (hasTransform)
                    transformed.insert(materialName);
                else
                    transformed.erase(materialName);
            }
        }
    }
    return transformed;
}
}
ModelData loadModel(const std::filesystem::path& input) {
    std::error_code ec;
    const auto path = std::filesystem::absolute(input, ec).lexically_normal();
    if (ec || !std::filesystem::is_regular_file(path, ec))
        throw std::runtime_error("Model file does not exist: " + foundation::pathToUtf8(input));
    // Match Assimp's content sniffing so renamed PMX files cannot bypass the
    // header checks or lose their material/opacity policy.
    const bool pmx = detail::lowerExtension(path) == ".pmx" || detail::hasPmxSignature(path);
    if (pmx) detail::validatePmxHeader(path);
    const bool obj = detail::lowerExtension(path) == ".obj";
    // Declared before importer so its observer remains alive during teardown.
    std::vector<std::filesystem::path> openedObjFiles;
    Assimp::Importer importer;
    importer.SetIOHandler(detail::createFileSystem(path, obj ? &openedObjFiles : nullptr));
    // Assimp normalizes UVs to a bottom-left origin (glTF2Importer flips Y).
    // Our uploaded stb image rows start at the top, so normalize back here.
    const auto* scene =
        importer.ReadFile(foundation::pathToUtf8(path),
                          aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
                              aiProcess_PreTransformVertices | aiProcess_ValidateDataStructure | aiProcess_FlipUVs);
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
        throw std::runtime_error("Cannot import " + foundation::pathToUtf8(path.filename()) + ": " +
                                 importer.GetErrorString());
    const auto objNormalTransforms = transformedObjNormals(openedObjFiles);
    ModelData model;
    model.name = foundation::pathToUtf8(path.filename());
    model.source = path;
    if (pmx)
        model.warnings.emplace_back(
            "PMX is displayed in its static rest pose; animation, morphs and physics are not applied");
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
                material.texture = detail::readTexture(*scene, texture, path.parent_path(), detail::isGltf(path),
                                                       &material.baseTexturePath);
                material.hasTexture = true;
            } catch (const std::exception& e) {
                model.warnings.push_back("Texture '" + std::string(texture.C_Str()) +
                                         "' unavailable; using white: " + e.what());
            }
        }
        // HEIGHT contains scalar bump data, including OBJ map_bump/bump. It
        // must never be decoded as an RGB tangent-space normal texture.
        if (obj && source.GetTextureCount(aiTextureType_HEIGHT) > 0)
            model.warnings.push_back("Material '" + material.name +
                                     "' uses an unsupported height/bump texture; ignoring the height texture");
        aiTextureMapping mapping = aiTextureMapping_UV;
        unsigned uvIndex = 0;
        if (source.GetTexture(aiTextureType_NORMALS, 0, &texture, &mapping, &uvIndex) == AI_SUCCESS) {
            material.normalTextureDeclared = true;
            source.Get(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0), material.normalScale);
            if (!std::isfinite(material.normalScale))
                throw std::runtime_error("Model contains a non-finite normal texture scale");
            aiUVTransform transform;
            if (mapping != aiTextureMapping_UV || uvIndex != 0) {
                model.warnings.push_back("Normal texture for material '" + material.name +
                                         "' requires unsupported texture coordinates; only UV0 is supported");
            } else if (objNormalTransforms.contains(material.name) ||
                       source.Get(AI_MATKEY_UVTRANSFORM(aiTextureType_NORMALS, 0), transform) == AI_SUCCESS) {
                model.warnings.push_back("Normal texture for material '" + material.name +
                                         "' uses an unsupported UV transform; keeping vertex normals");
            } else {
                try {
                    material.normalTexture =
                        detail::readTexture(*scene, texture, path.parent_path(), detail::isGltf(path));
                    material.hasNormalMap = true;
                    material.normalTextureSource = texture.C_Str();
                } catch (const std::exception& e) {
                    model.warnings.push_back("Normal texture '" + std::string(texture.C_Str()) +
                                             "' unavailable; using vertex normals: " + e.what());
                }
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
        mesh.hasUV0 = source.HasTextureCoords(0);
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
    if (pmx) detail::applyMmdEffectNormals(model);
    initializeModelMaterials(model);
    return model;
}
}
