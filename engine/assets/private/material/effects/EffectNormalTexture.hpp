#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace aeg::assets::detail {
struct EffectNormalTexture {
    std::filesystem::path image;
    std::string diagnostic;
};

// Text/resource metadata only: this does not compile or execute an effect.
EffectNormalTexture readEffectNormalTexture(const std::filesystem::path& effect);
std::string readEffectText(const std::filesystem::path& path);
std::filesystem::path resolveEffectReference(std::string_view reference, const std::filesystem::path& directory,
                                             bool stringEscapes = false);
}
