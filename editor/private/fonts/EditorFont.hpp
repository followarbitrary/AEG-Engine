#pragma once
#include <filesystem>

namespace aeg::editor {
struct EditorFontInfo {
    std::filesystem::path source;
    float pixelSize{18};
};

// Requires an active ImGui context. Loads and builds a Chinese-capable font
// before renderer initialization; font data is owned by the font atlas.
[[nodiscard]] EditorFontInfo configureEditorFont(const std::filesystem::path& resourceDirectory,
                                                 const std::filesystem::path& requestedFont = {});
}
