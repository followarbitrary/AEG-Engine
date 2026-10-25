#pragma once
#include <cstdint>
#include <filesystem>

namespace aeg {
struct EditorOptions {
    std::filesystem::path resourceDirectory;
    std::filesystem::path initialModel;
    std::filesystem::path capturePath;
    std::filesystem::path fontPath;
    std::uint32_t frameLimit{0};
    std::uint32_t msaaSamples{1};
    bool fxaa{false};
    bool validation{true};
    bool hidden{false};
};
int runEditor(const EditorOptions& options);
}
