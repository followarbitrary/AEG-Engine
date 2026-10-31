#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <imgui.h>

namespace aeg::editor::ui {

inline ImVec4 rgb(const std::uint32_t value, const float alpha = 1.0f) {
    const auto linear = [](const std::uint32_t channel) {
        const float srgb = static_cast<float>(channel) / 255.0f;
        return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    };
    return {linear((value >> 16) & 0xff), linear((value >> 8) & 0xff), linear(value & 0xff), alpha};
}

inline float dpi() {
    if (!ImGui::GetCurrentContext()) return 1.0f;
    const auto scale = ImGui::GetIO().DisplayFramebufferScale.x;
    return std::clamp(scale > 0.0f ? scale : 1.0f, 1.0f, 3.0f);
}

inline float px(const float value) { return value * dpi(); }
inline ImVec2 px(const float x, const float y) { return {px(x), px(y)}; }

inline void PaintPanelBackground() {
    auto* draw = ImGui::GetWindowDrawList();
    const auto min = ImGui::GetWindowPos();
    const auto size = ImGui::GetWindowSize();
    const ImVec2 max{min.x + size.x, min.y + size.y};
    draw->AddRectFilled(min, max, ImGui::GetColorU32(rgb(0x1a1e25)));
}

inline bool AccentButton(const char* label, const ImVec2 size = {0, 0}) {
    const auto accent = rgb(0x3fb4e6);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(5.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(0x63c7ee));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(0x2f9ccc));
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(0x06141c));
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return clicked;
}

inline bool SecondaryButton(const char* label, const ImVec2 size = {0, 0}) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(5.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, rgb(0x2a303a));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(0x343c48));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(0x3a4350));
    ImGui::PushStyleColor(ImGuiCol_Border, rgb(0x3a4350));
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return clicked;
}

inline bool SegmentedControl(const char* id, const char* const* labels, const int count, int& selected,
                             const float height = 32.0f) {
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(5.0f));
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float segmentWidth = count > 0 ? std::max(px(1.0f), availableWidth / static_cast<float>(count)) : 0.0f;
    for (int index = 0; index < count; ++index) {
        if (index > 0) ImGui::SameLine(0.0f, 0.0f);
        const bool active = index == selected;
        ImGui::PushStyleColor(ImGuiCol_Button, active ? rgb(0x3fb4e6) : rgb(0x20252d));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? rgb(0x63c7ee) : rgb(0x2a303a));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(0x3a4350));
        if (active) ImGui::PushStyleColor(ImGuiCol_Text, rgb(0x06141c));
        if (ImGui::Button(labels[index], {segmentWidth, px(height)})) {
            selected = index;
            changed = true;
        }
        if (active) ImGui::PopStyleColor();
        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar();
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
    // Keep the three axis fields unique across repeated rows such as position,
    // rotation, and scale. The axis index alone is not sufficient because each
    // row would otherwise create the same ##Axis/0, ##Axis/1, and ##Axis/2 IDs.
    ImGui::PushID(static_cast<const void*>(values));
    for (int axis = 0; axis < 3; ++axis) {
        if (axis > 0) ImGui::SameLine(0.0f, spacing);
        ImGui::PushID(axis);
        ImGui::BeginGroup();
        const auto chipMin = ImGui::GetCursorScreenPos();
        const ImVec2 chipSize{px(16.0f), ImGui::GetFrameHeight()};
        ImGui::Dummy(chipSize);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(chipMin, {chipMin.x + chipSize.x, chipMin.y + chipSize.y},
                            ImGui::GetColorU32(rgb(colors[axis])), px(3.0f));
        const auto textSize = ImGui::CalcTextSize(names[axis]);
        draw->AddText({chipMin.x + (chipSize.x - textSize.x) * 0.5f,
                       chipMin.y + (chipSize.y - textSize.y) * 0.5f},
                      ImGui::GetColorU32(rgb(0x06141c)), names[axis]);
        ImGui::SameLine(0.0f, px(3.0f));
        ImGui::SetNextItemWidth(std::max(px(28.0f), cellWidth - px(15.0f)));
        changed |= ImGui::DragFloat("##Axis", &values[axis], speed, minimum, maximum, "%.3f");
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

} // namespace aeg::editor::ui
