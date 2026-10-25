#pragma once
#include <filesystem>

namespace aeg::assets::detail {
bool hasPmxSignature(const std::filesystem::path& path);
void validatePmxHeader(const std::filesystem::path& path);
}
