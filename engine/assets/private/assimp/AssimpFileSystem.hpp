#pragma once
#include <filesystem>
#include <vector>

namespace Assimp {
class IOSystem;
}

namespace aeg::assets::detail {
// SetIOHandler takes ownership of the returned file system.
// The optional synchronous observer must outlive the importer/file system.
Assimp::IOSystem* createFileSystem(const std::filesystem::path& source,
                                   std::vector<std::filesystem::path>* openedFiles = nullptr);
}
