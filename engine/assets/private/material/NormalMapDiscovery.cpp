#include <MaterialTextures.hpp>
#include <Utf8Path.hpp>
#include "image/ImageDecoder.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace aeg::assets {
namespace {
namespace fs = std::filesystem;

std::string folded(std::string value) {
    for (auto& letter : value)
        if (letter >= 'A' && letter <= 'Z') letter = static_cast<char>(letter - 'A' + 'a');
    return value;
}
std::string key(const fs::path& path) { return folded(foundation::pathToUtf8(path.lexically_normal())); }

constexpr std::array<std::string_view, 4> normalDirectories{"normal", "normals", "normalmap", "normalmaps"};
constexpr std::array<std::string_view, 3> textureDirectories{"tex", "texture", "textures"};
constexpr std::array<std::string_view, 5> imageExtensions{".png", ".jpg", ".jpeg", ".tga", ".bmp"};
constexpr std::array<std::string_view, 9> normalSuffixes{"_normal",    "_normals",    "_n",     "_nrm", "_nor",
                                                         "_normalmap", "_normal_map", "normal", "n"};
constexpr std::array<std::string_view, 7> colorSuffixes{"_base_color", "_basecolor", "_albedo", "_diffuse",
                                                        "_color",      "_base",      "_d"};

template <std::size_t N> bool oneOf(std::string_view name, const std::array<std::string_view, N>& choices) {
    return std::find(choices.begin(), choices.end(), name) != choices.end();
}
fs::path absolutePath(const fs::path& path, const fs::path& directory = {}) {
    if (path.empty()) return {};
    std::error_code error;
    const auto result = fs::absolute(path.is_absolute() ? path : directory / path, error);
    return error ? fs::path{} : result.lexically_normal();
}

struct DirectoryListing {
    std::vector<fs::path> files;
    std::vector<fs::path> directories;
};
class DirectoryIndex {
public:
    const DirectoryListing& list(const fs::path& directory) {
        const auto [position, inserted] = listings_.try_emplace(key(directory));
        if (!inserted || directory.empty()) return position->second;
        auto& listing = position->second;
        std::error_code error;
        fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error), end;
        while (!error && iterator != end) {
            const auto& entry = *iterator;
            std::error_code kindError;
            if (entry.is_directory(kindError))
                listing.directories.push_back(entry.path());
            else if (!kindError && entry.is_regular_file(kindError) &&
                     oneOf(folded(foundation::pathToUtf8(entry.path().extension())), imageExtensions))
                listing.files.push_back(entry.path());
            iterator.increment(error);
        }
        const auto less = [](const fs::path& a, const fs::path& b) {
            const auto first = key(a), second = key(b);
            return first == second ? foundation::pathToUtf8(a) < foundation::pathToUtf8(b) : first < second;
        };
        std::sort(listing.files.begin(), listing.files.end(), less);
        std::sort(listing.directories.begin(), listing.directories.end(), less);
        return listing;
    }

private:
    std::map<std::string, DirectoryListing> listings_;
};

struct SearchDirectory {
    fs::path path;
    bool normalDirectory{};
};
std::vector<SearchDirectory> searchDirectories(DirectoryIndex& index, const fs::path& modelDirectory,
                                               const fs::path& basePath) {
    std::map<std::string, SearchDirectory> roots;
    auto addRoot = [&](const fs::path& path) {
        if (!path.empty()) roots.try_emplace(key(path), SearchDirectory{path, false});
    };
    if (!basePath.empty()) addRoot(basePath.parent_path());
    addRoot(modelDirectory);
    if (!modelDirectory.empty())
        for (const auto& directory : index.list(modelDirectory).directories)
            if (oneOf(folded(foundation::pathToUtf8(directory.filename())), textureDirectories)) addRoot(directory);

    auto result = roots;
    for (const auto& [rootKey, root] : roots) {
        (void)rootKey;
        for (const auto& directory : index.list(root.path).directories) {
            if (!oneOf(folded(foundation::pathToUtf8(directory.filename())), normalDirectories)) continue;
            const auto [position, inserted] = result.try_emplace(key(directory), SearchDirectory{directory, true});
            if (!inserted) position->second.normalDirectory = true;
        }
    }
    std::vector<SearchDirectory> directories;
    for (auto& [directoryKey, directory] : result) {
        (void)directoryKey;
        directories.push_back(std::move(directory));
    }
    return directories;
}

std::vector<std::string> identities(const MaterialData& material, const fs::path& basePath, const fs::path& modelPath,
                                    bool singleMaterial) {
    std::vector<std::string> result;
    auto add = [&](std::string identity) {
        identity = folded(std::move(identity));
        if (!identity.empty() && std::find(result.begin(), result.end(), identity) == result.end())
            result.push_back(std::move(identity));
    };
    if (!basePath.empty()) {
        const auto stem = folded(foundation::pathToUtf8(basePath.stem()));
        add(stem);
        for (const auto suffix : colorSuffixes)
            if (stem.size() > suffix.size() && stem.ends_with(suffix)) {
                add(stem.substr(0, stem.size() - suffix.size()));
                break;
            }
    }
    add(material.name);
    if (singleMaterial && !modelPath.empty()) add(foundation::pathToUtf8(modelPath.stem()));
    return result;
}

std::vector<fs::path> candidates(DirectoryIndex& index, const std::vector<SearchDirectory>& directories,
                                 const std::string& identity, const fs::path& basePath) {
    std::set<std::string> normalNames;
    for (const auto suffix : normalSuffixes) normalNames.insert(identity + std::string(suffix));
    normalNames.insert("normal_" + identity);
    normalNames.insert("n_" + identity);
    std::map<std::string, fs::path> found;
    const auto baseKey = key(basePath);
    for (const auto& directory : directories)
        for (const auto& file : index.list(directory.path).files) {
            if (!basePath.empty() && key(file) == baseKey) continue;
            const auto stem = folded(foundation::pathToUtf8(file.stem()));
            if (normalNames.contains(stem) || (directory.normalDirectory && stem == identity))
                found.try_emplace(key(file), file);
        }
    std::vector<fs::path> matches;
    for (auto& [fileKey, file] : found) {
        (void)fileKey;
        matches.push_back(std::move(file));
    }
    return matches;
}

void warn(ModelData& model, std::string warning) {
    if (std::find(model.warnings.begin(), model.warnings.end(), warning) == model.warnings.end())
        model.warnings.push_back(std::move(warning));
}
}

void initializeModelMaterials(ModelData& model) {
    auto candidate = model;
    std::set<std::size_t> usedMaterials;
    for (const auto& mesh : candidate.meshes) {
        if (mesh.materialIndex >= candidate.materials.size())
            throw std::runtime_error("Normal texture references an invalid material");
        usedMaterials.insert(mesh.materialIndex);
    }
    const auto modelPath = absolutePath(candidate.source);
    const auto modelDirectory = modelPath.empty() ? fs::path{} : modelPath.parent_path();
    DirectoryIndex index;
    for (const auto materialIndex : usedMaterials) {
        auto& material = candidate.materials[materialIndex];
        if (material.hasNormalMap || material.normalTextureDeclared) continue;
        const auto basePath = absolutePath(material.baseTexturePath, modelDirectory);
        // Procedural models with no path metadata must not inspect unrelated cwd assets.
        if (modelDirectory.empty() && basePath.empty()) continue;
        const auto directories = searchDirectories(index, modelDirectory, basePath);
        for (const auto& identity : identities(material, basePath, modelPath, usedMaterials.size() == 1)) {
            const auto matches = candidates(index, directories, identity, basePath);
            if (matches.empty()) continue;
            if (matches.size() > 1) {
                std::string message = "Ambiguous normal textures for material '" + material.name + "': ";
                for (std::size_t i = 0; i < matches.size(); ++i) {
                    if (i) message += ", ";
                    message += foundation::pathToUtf8(matches[i]);
                }
                warn(candidate, std::move(message) + "; choose the normal texture manually");
                break;
            }
            try {
                auto image = detail::readTexture(matches.front());
                auto source = foundation::pathToUtf8(matches.front());
                material.normalTexture = std::move(image);
                material.normalTextureSource = std::move(source);
                material.normalScale = 1;
                material.hasNormalMap = true;
            } catch (const std::runtime_error& failure) {
                warn(candidate, "Normal texture '" + foundation::pathToUtf8(matches.front()) +
                                    "' unavailable; using vertex normals: " + failure.what());
            }
            break;
        }
    }
    prepareModelMaterials(candidate);
    static_assert(std::is_nothrow_move_assignable_v<ModelData>);
    model = std::move(candidate);
}
}
