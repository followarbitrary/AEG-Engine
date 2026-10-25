#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace aeg::foundation {
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text);
}
