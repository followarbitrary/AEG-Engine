#pragma once
#include <ModelData.hpp>
#include <filesystem>

namespace aeg::assets {
[[nodiscard]] ModelData loadModel(const std::filesystem::path& path);
}
