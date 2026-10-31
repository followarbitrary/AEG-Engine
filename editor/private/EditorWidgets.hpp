#pragma once

#include <algorithm>
#include <cstdint>

#include <imgui.h>

namespace aeg::editor::ui {

inline ImVec4 rgb(const std::uint32_t value, const float alpha = 1.0f) {
    return {static_cast<float>((value >> 16) & 0xff) / 255.0f,
            static_cast<float>((value >> 8) & 0xff) / 255.0f,
            static_cast<float>(value & 0xff) / 255.0f, alpha};
}

inline float dpi() {
    if (!ImGui::GetCurrentContext()) return 1.0f;
    const auto scale = ImGui::GetIO().DisplayFramebufferScale.x;
    return std::clamp(scale > 0.0f ? scale : 1.0f, 1.0f, 3.0f);
}

inline float px(const float value) { return value * dpi(); }
inline ImVec2 px(const float x, const float y) { return {px(x), px(y)}; }

inline bool AccentButton(const char* label, const ImVec2 size = {0, 0}) {
    const auto accent = rgb(0x3fb4e6);
    ImGui::PushStyleColor(ImGuiCol_Button, accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(0x63c7ee));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(0x2f9ccc));
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(0x06141c));
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

inline bool SecondaryButton(const char* label, const ImVec2 size = {0, 0}) {
    ImGui::PushStyleColor(ImGuiCol_Button, rgb(0x2a303a));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(0x343c48));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(0x3a4350));
    ImGui::PushStyleColor(ImGuiCol_Border, rgb(0x3a4350));
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

inline bool SegmentedControl(const char* id, const char* const* labels, const int count, int& selected,
                             const float height = 32.0f) {
    bool changed = false;
    ImGui::PushID(id);
    for (int index = 0; index < count; ++index) {
        if (index > 0) ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, index == selected ? rgb(0x3fb4e6) : rgb(0x20252d));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, index == selected ? rgb(0x63c7ee) : rgb(0x2a303a));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(0x3a4350));
        if (index == selected) ImGui::PushStyleColor(ImGuiCol_Text, rgb(0x06141c));
        if (ImGui::Button(labels[index], {-1.0f, px(height)})) {
            selected = index;
            changed = true;
        }
        if (index == selected) ImGui::PopStyleColor();
        ImGui::PopStyleColor(3);
    }
    ImGui::PopID();
    return changed;
}

inline bool SectionHeader(const char* label, const char* summary = nullptr,
                          const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None) {
    const bool open = ImGui::CollapsingHeader(label, flags);
    if (summary) {
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        const auto size = ImGui::CalcTextSize(summary);
        ImGui::GetWindowDrawList()->AddText({max.x - size.x - px(10.0f), min.y + (max.y - min.y - size.y) * 0.5f},
                                            ImGui::GetColorU32(ImGuiCol_TextDisabled), summary);
    }
    return open;
}

inline bool AxisField3(const char* label, float values[3], const float speed, const float minimum = 0.0f,
                       const float maximum = 0.0f) {
    constexpr const char* names[3]{"X", "Y", "Z"};
    constexpr std::uint32_t colors[3]{0xe5645a, 0x6cc46a, 0x5a8fe5};
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(px(38.0f));
    bool changed = false;
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float cellWidth = std::max(px(52.0f), (ImGui::GetContentRegionAvail().x - 2.0f * spacing) / 3.0f);
    for (int axis = 0; axis < 3; ++axis) {
        if (axis > 0) ImGui::SameLine(0.0f, spacing);
        ImGui::PushID(axis);
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(colors[axis]));
        ImGui::TextUnformatted(names[axis]);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, px(3.0f));
        ImGui::SetNextItemWidth(std::max(px(28.0f), cellWidth - px(15.0f)));
        changed |= ImGui::DragFloat("##Axis", &values[axis], speed, minimum, maximum, "%.3f");
        ImGui::EndGroup();
        ImGui::PopID();
    }
    return changed;
}

} // namespace aeg::editor::ui
