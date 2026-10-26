#include <ModelImporter.hpp>
#include <MaterialTextures.hpp>
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
namespace fs = std::filesystem;
using aeg::assets::ModelData;
std::string utf8(std::u8string_view value) { return {reinterpret_cast<const char*>(value.data()), value.size()}; }
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
class Temporary {
public:
    fs::path root;
    Temporary() {
        parent_ = fs::absolute(fs::temp_directory_path()).lexically_normal();
        std::random_device random;
        for (int attempt = 0; attempt < 32; ++attempt) {
            root = parent_ /
                   ("aeg-emd-normal-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                    "-" + std::to_string(random()));
            if (fs::create_directory(root)) return;
        }
        throw std::runtime_error("Cannot create owned EMD test directory");
    }
    ~Temporary() {
        if (!root.is_absolute() || root.parent_path() != parent_ ||
            !root.filename().string().starts_with("aeg-emd-normal-"))
            return;
        std::error_code error;
        fs::remove_all(root, error);
    }
    fs::path copy(const fs::path& relative, const fs::path& original) const {
        const auto target = root / relative;
        fs::create_directories(target.parent_path());
        fs::copy_file(fs::path(AEG_TEST_FIXTURES) / original, target);
        return target;
    }
    void text(const fs::path& relative, const std::string& value) const {
        const auto target = root / relative;
        fs::create_directories(target.parent_path());
        std::ofstream file(target, std::ios::binary);
        file.write(value.data(), static_cast<std::streamsize>(value.size()));
        require(bool(file), "EMD test data write failed");
    }
    fs::path model() const {
        const auto path = copy("Scene.pmx", "pmx-utf8.pmx");
        copy(fs::path(u8"pmx-textures/\u5f69\u8272 pad%20.png"), fs::path(u8"pmx-textures/\u5f69\u8272 pad%20.png"));
        return path;
    }
    void effect(const std::string& body, const std::string& binding = "Obj=none\nObj[0]=effects/Body.fx\n") const {
        text("Scene.emd", "[Info]\nVersion=3\n[Effect]\n" + binding);
        text("effects/Body.fx", body);
    }

private:
    fs::path parent_;
};
constexpr std::string_view enabled = "#define USE_NORMALMAP\n#define TEXTURE_NORMALMAP \"unrelated-image.png\"\n";
void mapped(const ModelData& value, std::size_t index, const fs::path& image) {
    const auto& material = value.materials.at(index);
    require(material.hasNormalMap && material.normalTextureDeclared,
            "EMD normal resource is an authoritative mapped material");
    require(material.normalTexture.width == 2 && material.normalTexture.height == 2 && material.normalScale == 1,
            "EMD image keeps its bytes and unit normal scale");
    require(material.normalTextureSource == aeg::foundation::pathToUtf8(image.lexically_normal()),
            "EMD binding records image resolved beside entry FX");
    bool used = false;
    for (const auto& mesh : value.meshes)
        if (mesh.materialIndex == index) {
            used = true;
            for (const auto& vertex : mesh.vertices)
                require(std::abs(vertex.tangent.w) == 1 &&
                            std::abs(glm::length(glm::vec3(vertex.tangent)) - 1) < 0.001f &&
                            std::abs(glm::dot(vertex.normal, glm::vec3(vertex.tangent))) < 0.001f,
                        "common material preparation builds finite orthogonal EMD tangent frames");
        }
    require(used, "expected EMD material is visible geometry");
}
void unmapped(const ModelData& value, std::size_t index) {
    require(!value.materials.at(index).hasNormalMap && value.materials.at(index).normalTextureDeclared,
            "explicit no-effect or failed EMD binding blocks filename guesses");
}
void diagnostic(const ModelData& value) {
    // PMX's static-rest-pose notice is always present, separately from EMD errors.
    require(value.warnings.size() > 1, "unsupported EMD input must add a diagnostic");
}
void decoys(const Temporary& files) {
    files.copy(fs::path(u8"pmx-textures/\u5f69\u8272 pad%20N.png"), "normal-map.png");
    files.copy("Solid_n.png", "normal-map.png");
}
std::string utf16(std::u16string_view value, bool little) {
    std::string bytes = little ? std::string("\xff\xfe", 2) : std::string("\xfe\xff", 2);
    for (const char16_t letter : value) {
        const auto low = static_cast<char>(letter & 0xff);
        const auto high = static_cast<char>(letter >> 8);
        bytes += little ? low : high;
        bytes += little ? high : low;
    }
    return bytes;
}
void explicitEffectBinding() {
    Temporary files;
    const auto source = files.model();
    files.text("Scene.emd", "[Info]\nVersion=3\n[Effect]\nObj=none\nObj[0]=effects/Body.fx\n");
    files.text("effects/Body.fx", "#define USE_NORMALMAP\n#define TEXTURE_NORMALMAP \"unrelated-image.png\"\n"
                                  "float NormalMapResolution = 1;\n");
    const auto image = files.copy("effects/unrelated-image.png", "normal-map.png");
    const auto value = aeg::assets::loadModel(source);
    require(value.materials.at(0).hasNormalMap, "EMD-bound normal must load without filename matching");
    require(value.materials.at(0).normalTextureSource == aeg::foundation::pathToUtf8(image.lexically_normal()),
            "EMD binding resolves image beside entry FX");
    require(!value.materials.at(1).hasNormalMap, "default none keeps unrelated material unchanged");
    mapped(value, 0, image);
    unmapped(value, 1);
}
void defaultOverridesAndDuplicates() {
    Temporary files;
    const auto source = files.model();
    files.effect(std::string(enabled), "Obj[1]=none\nObj=missing.fx\nObj[1]=effects/Other.fx\nObj=effects/Body.fx\n");
    files.text("effects/Other.fx", "#define USE_NORMALMAP\n#define TEXTURE_NORMALMAP \"second.png\"\n");
    const auto first = files.copy("effects/unrelated-image.png", "normal-map.png");
    const auto second = files.copy("effects/second.png", "normal-map.png");
    const auto value = aeg::assets::loadModel(source);
    mapped(value, 0, first);
    mapped(value, 1, second);
    require(value.warnings.size() == 1, "last duplicate defaults/overrides are applied without spurious errors");
}
void explicitNoneBlocksGuesses() {
    Temporary files;
    const auto source = files.model();
    decoys(files);
    files.effect(std::string(enabled), "Obj=effects/Body.fx\nObj[0]=NoNe\nObj[1]=\n");
    files.copy("effects/unrelated-image.png", "normal-map.png");
    const auto value = aeg::assets::loadModel(source);
    unmapped(value, 0);
    unmapped(value, 1);
    require(value.warnings.size() == 1, "explicit none/empty is a quiet authored disable");
}
void commentsAliasesAndConditions() {
    Temporary files;
    const auto source = files.model();
    files.effect("/* #define TEXTURE_NORMALMAP \"wrong0.png\" */\n"
                 "#define USE_NORMALMAP\n#define PICK 1\n"
                 "#if 0\n#define TEXTURE_NORMALMAP \"wrong1.png\"\n"
                 "#elif defined(PICK) && !defined(DISABLED)\n"
                 "#define RESOURCE \"maps//literal; percent%20+ space.png\"\n"
                 "#define TEXTURE_NORMALMAP RESOURCE // exact alias, not a guessed texture\n"
                 "#else\n#define TEXTURE_NORMALMAP \"wrong2.png\"\n#endif\n"
                 "#undef PICK\n#ifndef PICK\n#define UNUSED 1\n#endif\n");
    const auto image = files.copy("effects/maps/literal; percent%20+ space.png", "normal-map.png");
    for (const auto* name : {"wrong0.png", "wrong1.png", "wrong2.png"})
        files.copy(fs::path("effects") / name, "normal-map.png");
    const auto value = aeg::assets::loadModel(source);
    mapped(value, 0, image);
    require(value.warnings.size() == 1, "comments and known inactive branches do not create errors");
}
void nestedIncludesUseEntryResourceDirectory() {
    Temporary files;
    const auto source = files.model();
    files.effect("#define CONFIG \"settings/Config.fxsub\"\n#include CONFIG\n");
    files.text("effects/settings/Config.fxsub",
               "#ifndef CONFIG_GUARD\n#define CONFIG_GUARD\n#include \"deeper/Normal.fxsub\"\n#endif\n");
    files.text("effects/settings/deeper/Normal.fxsub", std::string(enabled));
    const auto image = files.copy("effects/unrelated-image.png", "normal-map.png");
    files.copy("effects/settings/deeper/unrelated-image.png", "pmx-textures/cutout.png");
    const auto value = aeg::assets::loadModel(source);
    mapped(value, 0, image);
    require(value.warnings.size() == 1, "include and resource directories are resolved independently");
}
void unicodeAndBomText() {
    for (int encoding = 0; encoding < 3; ++encoding) {
        Temporary files;
        const auto source = files.model();
        const std::u16string emd = u"[Info]\nVersion=3\n[Effect]\nObj=none\nObj[0]=effects/\u6548\u679c %20.fx\n";
        const std::u16string fx = u"#define USE_NORMALMAP\n#define TEXTURE_NORMALMAP \"\u6cd5\u7ebf +%20.png\"\n";
        if (encoding == 0) {
            files.text("Scene.emd",
                       std::string("\xef\xbb\xbf", 3) +
                           utf8(u8"[Info]\nVersion=3\n[Effect]\nObj=none\nObj[0]=effects/\u6548\u679c %20.fx\n"));
            files.text(fs::path(u8"effects/\u6548\u679c %20.fx"),
                       std::string("\xef\xbb\xbf", 3) +
                           utf8(u8"#define USE_NORMALMAP\n#define TEXTURE_NORMALMAP \"\u6cd5\u7ebf +%20.png\"\n"));
        } else {
            files.text("Scene.emd", utf16(emd, encoding == 1));
            files.text(fs::path(u8"effects/\u6548\u679c %20.fx"), utf16(fx, encoding == 1));
        }
        const auto image = files.copy(fs::path(u8"effects/\u6cd5\u7ebf +%20.png"), "normal-map.png");
        const auto value = aeg::assets::loadModel(source);
        mapped(value, 0, image);
    }
}
void legacyCommentsAndPaths() {
    Temporary files;
    const auto source = files.model();
    files.effect("#define USE_NORMALMAP\n#include \"Legacy.fxsub\"\n");
    files.text("effects/Legacy.fxsub", std::string("// Legacy CP932 comment ") + "\x82\xa0\x82\xa2\n" +
                                           "#define TEXTURE_NORMALMAP \"unrelated-image.png\"\n");
    const auto image = files.copy("effects/unrelated-image.png", "normal-map.png");
    mapped(aeg::assets::loadModel(source), 0, image);
#ifdef _WIN32
    Temporary legacy;
    const auto legacySource = legacy.model();
    const std::string sjis = "\x83\x65\x83\x58\x83\x67"; // テスト, CP932.
    legacy.text("Scene.emd", "[Info]\nVersion=3\n[Effect]\nObj=none\nObj[0]=effects/" + sjis + ".fx\n");
    legacy.text(fs::path(u8"effects/\u30c6\u30b9\u30c8.fx"),
                "#define USE_NORMALMAP\n#define TEXTURE_NORMALMAP \"" + sjis + ".png\"\n");
    const auto legacyImage = legacy.copy(fs::path(u8"effects/\u30c6\u30b9\u30c8.png"), "normal-map.png");
    mapped(aeg::assets::loadModel(legacySource), 0, legacyImage);
#endif
}
void indexErrorsAndSections() {
    Temporary files;
    const auto source = files.model();
    files.text("Scene.emd", "[Info]\nVersion=3\nObj[0]=not-an-effect-section.fx\n"
                            "[eFfEcT]\noBJ=none\nOBJ[1]=effects/Body.fx\n"
                            "Obj[99]=missing.fx\nObj[-1]=missing.fx\nObj[bad]=missing.fx\n");
    files.text("effects/Body.fx", std::string(enabled));
    const auto image = files.copy("effects/unrelated-image.png", "normal-map.png");
    const auto value = aeg::assets::loadModel(source);
    unmapped(value, 0);
    mapped(value, 1, image);
    diagnostic(value);
}
void missingAndCorruptResources() {
    for (int failure = 0; failure < 4; ++failure) {
        Temporary files;
        const auto source = files.model();
        decoys(files);
        files.effect(std::string(enabled));
        if (failure == 0) fs::remove(files.root / "effects/Body.fx");
        if (failure == 1) files.text("effects/unrelated-image.png", "not an image");
        if (failure == 2) files.text("effects/Body.fx", "#include \"missing.fxsub\"\n" + std::string(enabled));
        // failure3 deliberately leaves the resource image absent.
        const auto value = aeg::assets::loadModel(source);
        unmapped(value, 0);
        unmapped(value, 1);
        diagnostic(value);
    }
}
void recursionAndResourceLimits() {
    for (int failure = 0; failure < 5; ++failure) {
        Temporary files;
        const auto source = files.model();
        files.effect(std::string(enabled));
        files.copy("effects/unrelated-image.png", "normal-map.png");
        if (failure == 0) {
            files.text("effects/Body.fx", "#include \"Cycle.fxsub\"\n" + std::string(enabled));
            files.text("effects/Cycle.fxsub", "#include \"Body.fx\"\n");
        } else if (failure == 1) {
            for (int index = 0; index < 20; ++index)
                files.text(fs::path("effects") / (index == 0 ? "Body.fx" : std::to_string(index) + ".fxsub"),
                           "#include \"" + std::to_string(index + 1) + ".fxsub\"\n");
            files.text("effects/20.fxsub", std::string(enabled));
        } else if (failure == 2) {
            files.text("effects/Body.fx", std::string(8 * 1024 * 1024 + 1, ' '));
        } else if (failure == 3) {
            std::string body(enabled);
            for (int index = 0; index < 70; ++index) {
                const auto name = std::to_string(index) + ".fxsub";
                body += "#include \"" + name + "\"\n";
                files.text(fs::path("effects") / name, "// bounded unique include\n");
            }
            files.text("effects/Body.fx", body);
        } else {
            files.text("effects/Body.fx",
                       "#include \"A.fxsub\"\n#include \"B.fxsub\"\n#include \"C.fxsub\"\n" + std::string(enabled));
            for (const auto* name : {"A.fxsub", "B.fxsub", "C.fxsub"})
                files.text(fs::path("effects") / name, "//" + std::string(6 * 1024 * 1024, 'x') + "\n");
        }
        const auto value = aeg::assets::loadModel(source);
        unmapped(value, 0);
        diagnostic(value);
    }
}
void unsupportedOrDisabledDeclarations() {
    const std::vector<std::string> inputs{
        "#define TEXTURE_NORMALMAP \"unrelated-image.png\"\n",
        "#define USE_NORMALMAP\n#define TEXTURE_NORMALMAP chooseTexture()\n",
        "#define USE_NORMALMAP\n#if DEVICE_FEATURE(1)\n#define TEXTURE_NORMALMAP \"unrelated-image.png\"\n#endif\n",
        std::string(enabled) + "float NormalMapResolution = 2;\n",
        std::string(enabled) + "float NormalMapResolution = chooseScale();\n",
        std::string(enabled) + "#undef USE_NORMALMAP\n",
        "#define USE_NORMALMAP\n#define TEXTURE_NORMALMAP A\n#define A B\n#define B A\n"};
    for (const auto& input : inputs) {
        Temporary files;
        const auto source = files.model();
        decoys(files);
        files.effect(input);
        files.copy("effects/unrelated-image.png", "normal-map.png");
        const auto value = aeg::assets::loadModel(source);
        unmapped(value, 0);
        diagnostic(value);
        require(value.materials[0].normalScale == 1, "UV resolution must never become normal strength");
    }
}
void logicalLineSplicing() {
    for (const bool splitEnable : {false, true}) {
        Temporary files;
        const auto source = files.model();
        files.effect(std::string(splitEnable ? "#define USE_NORMAL\\\nMAP\n" : "#define USE_NORMALMAP\n") +
                     "#define TEXTURE_NORMALMAP \"unrelated-\\\nimage.png\"\n");
        const auto image = files.copy("effects/unrelated-image.png", "normal-map.png");
        files.copy("effects/unrelated-  image.png", "normal-map.png");
        const auto value = aeg::assets::loadModel(source);
        mapped(value, 0, image);
        require(value.warnings.size() == 1, "backslash-newline splicing does not insert spaces or split identifiers");
    }
}
void compoundConditionMacroPrecedence() {
    for (const bool parenthesized : {false, true}) {
        Temporary files;
        const auto source = files.model();
        const std::string flag = parenthesized ? "(1 || 0)" : "1 || 0";
        const std::string first = parenthesized ? "wrong.png" : "unrelated-image.png";
        const std::string second = parenthesized ? "unrelated-image.png" : "wrong.png";
        files.effect("#define USE_NORMALMAP\n#define FLAG " + flag +
                     "\n#if FLAG && 0\n"
                     "#define TEXTURE_NORMALMAP \"" +
                     first +
                     "\"\n#else\n"
                     "#define TEXTURE_NORMALMAP \"" +
                     second + "\"\n#endif\n");
        const auto image = files.copy("effects/unrelated-image.png", "normal-map.png");
        files.copy("effects/wrong.png", "normal-map.png");
        const auto value = aeg::assets::loadModel(source);
        if (parenthesized) {
            mapped(value, 0, image);
            require(value.warnings.size() == 1, "parenthesized compound macro has an unambiguous false condition");
        } else {
            unmapped(value, 0);
            diagnostic(value);
        }
    }
    Temporary files;
    const auto source = files.model();
    files.effect("#define USE_NORMALMAP\n#if UNKNOWN_FN()\n#define FLAG 1 || 0\n"
                 "#else\n#define FLAG 0\n#endif\n#if FLAG && 0\n"
                 "#define TEXTURE_NORMALMAP \"unrelated-image.png\"\n#else\n"
                 "#define TEXTURE_NORMALMAP \"wrong.png\"\n#endif\n");
    files.copy("effects/unrelated-image.png", "normal-map.png");
    files.copy("effects/wrong.png", "normal-map.png");
    const auto value = aeg::assets::loadModel(source);
    unmapped(value, 0);
    diagnostic(value);
}
void manualControlsAndFallback() {
    Temporary files;
    const auto source = files.model();
    decoys(files);
    auto fallback = aeg::assets::loadModel(source);
    require(fallback.materials[0].hasNormalMap && !fallback.materials[0].normalTextureDeclared,
            "missing EMD retains conventional filename discovery");
    files.effect(std::string(enabled));
    const auto image = files.copy("effects/unrelated-image.png", "normal-map.png");
    auto value = aeg::assets::loadModel(source);
    mapped(value, 0, image);
    aeg::assets::clearMaterialNormalMap(value, 0);
    aeg::assets::prepareModelMaterials(value);
    require(!value.materials[0].hasNormalMap, "manual clear stays cleared after common preparation");
    aeg::assets::setMaterialNormalMap(value, 0, aeg::assets::ImageData{1, 1, {204, 128, 230, 255}}, -0.5f);
    aeg::assets::initializeModelMaterials(value);
    require(value.materials[0].normalTexture.width == 1 && value.materials[0].normalScale == -0.5f &&
                value.materials[0].normalTextureSource.empty(),
            "manual image replacement stays authoritative over initialization");
    const auto nativePath = files.copy("Native.gltf", "normal-map.gltf");
    files.text("Native.emd", "[Effect]\nObj=missing.fx\n");
    const auto native = aeg::assets::loadModel(nativePath);
    const auto& nativeMaterial = native.materials.at(native.meshes.at(0).materialIndex);
    require(nativeMaterial.hasNormalMap && nativeMaterial.normalTextureDeclared &&
                nativeMaterial.normalScale == 0.375f && native.warnings.empty(),
            "native glTF normal stays unchanged by unrelated MMD sidecar files");
}
}
int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--inspect") {
        try {
            const auto input = aeg::foundation::pathFromUtf8(argv[2]);
            std::vector<fs::path> paths;
            if (fs::is_directory(input)) {
                for (const auto& entry : fs::directory_iterator(input)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".pmx") continue;
                    auto emd = entry.path();
                    emd.replace_extension(".emd");
                    if (fs::is_regular_file(emd)) paths.push_back(entry.path());
                }
                std::sort(paths.begin(), paths.end());
                require(!paths.empty(), "inspect directory has no PMX/EMD pair");
            } else {
                paths.push_back(input);
            }
            for (const auto& source : paths) {
                const auto value = aeg::assets::loadModel(source);
                std::cout << "MODEL " << aeg::foundation::pathToUtf8(source) << '\n';
                std::size_t mappedCount = 0;
                for (std::size_t index = 0; index < value.materials.size(); ++index) {
                    const auto& material = value.materials[index];
                    if (!material.hasNormalMap) continue;
                    ++mappedCount;
                    std::cout << "MAP " << index << " name=" << material.name
                              << " base=" << aeg::foundation::pathToUtf8(material.baseTexturePath)
                              << " normal=" << material.normalTextureSource << '\n';
                }
                std::cout << "MATERIALS " << value.materials.size() << " MAPPED " << mappedCount << '\n';
                for (const auto& warning : value.warnings) std::cout << "WARNING " << warning << '\n';
            }
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "FAIL EMD inspect: " << error.what() << '\n';
            return 1;
        }
    }
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"explicit EMD effect normal binding", explicitEffectBinding},
        {"EMD default overrides and duplicates", defaultOverridesAndDuplicates},
        {"EMD none prevents filename guesses", explicitNoneBlocksGuesses},
        {"FX comments aliases and conditionals", commentsAliasesAndConditions},
        {"nested include resource directories", nestedIncludesUseEntryResourceDirectory},
        {"EMD FX Unicode and text BOMs", unicodeAndBomText},
        {"legacy comments and path literals", legacyCommentsAndPaths},
        {"EMD indices and section filtering", indexErrorsAndSections},
        {"missing or corrupt effect resources", missingAndCorruptResources},
        {"FX recursion and resource limits", recursionAndResourceLimits},
        {"unsupported or disabled normal declarations", unsupportedOrDisabledDeclarations},
        {"FX logical line splicing", logicalLineSplicing},
        {"compound condition macro precedence", compoundConditionMacroPrecedence},
        {"manual controls native precedence and filename fallback", manualControlsAndFallback}};
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
