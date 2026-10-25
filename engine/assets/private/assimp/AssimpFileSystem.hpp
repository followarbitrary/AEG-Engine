#pragma once
#include <filesystem>

namespace Assimp { class IOSystem; }

namespace aeg::assets::detail {
// SetIOHandler takes ownership of the returned file system.
Assimp::IOSystem* createFileSystem(const std::filesystem::path& source);
}
