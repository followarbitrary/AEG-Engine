#include "EditorState.hpp"
#include <ToneMappingSettings.hpp>
#include <exception>
#include <string>

namespace aeg::editor {
void drawToneMappingSection(EditorState& state, render::Renderer& renderer) {
    auto candidate = renderer.toneMapping();
    const auto mapperLabel = candidate.mapper == render::ToneMapper::Agx ? "AgX" :
                             candidate.mapper == render::ToneMapper::Aces ? "ACES" : "线性";
    const auto summary = std::string(mapperLabel) + " · " + std::to_string(candidate.exposureEV) + " EV";
    if (!ui::SectionHeader("色调映射###Tone mapping", summary.c_str())) return;
    constexpr const char* names[]{"线性（对照）", "ACES fitted", "AgX"};
    int mapper = static_cast<int>(candidate.mapper);
    bool changed = ImGui::Combo("映射###Tone mapper", &mapper, names, 3);
    if (changed) candidate.mapper = static_cast<render::ToneMapper>(mapper);
    changed |= ImGui::SliderFloat("曝光###Exposure EV", &candidate.exposureEV, -12, 12, "%.2f EV",
                                  ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::Button("重置曝光与映射###Reset tone mapping", {-1, 0})) {
        candidate = {};
        changed = true;
    }
    if (changed) {
        try {
            renderer.setToneMapping(candidate);
            state.toneMappingError.clear();
        } catch (const std::exception& error) {
            state.toneMappingError = std::string("色调映射设置失败：") + error.what();
        }
    }
    ImGui::TextWrapped("曝光以 EV 为单位，+1 加倍、-1 减半。映射在 HDR TAA 之后执行，调整曝光会保留历史。");
    if (!state.toneMappingError.empty()) ImGui::TextWrapped("%s", state.toneMappingError.c_str());
}
}
