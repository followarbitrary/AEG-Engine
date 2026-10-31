#include "EditorState.hpp"

namespace aeg::editor {
void configureStyle() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    const float scale = ui::dpi();
    const auto px = [scale](const float value) { return value * scale; };

    style.WindowPadding = {px(10.0f), px(8.0f)};
    style.FramePadding = {px(7.0f), px(5.0f)};
    style.ItemSpacing = {px(7.0f), px(6.0f)};
    style.ItemInnerSpacing = {px(5.0f), px(4.0f)};
    style.IndentSpacing = px(16.0f);
    style.WindowRounding = 0.0f;
    // Keep the shell squared off while giving interactive controls a soft,
    // deliberate corner.  The reference UI uses a small radius rather than
    // pill-shaped controls, so scale the values with the active DPI.
    style.ChildRounding = px(4.0f);
    style.FrameRounding = px(5.0f);
    style.PopupRounding = px(5.0f);
    style.GrabRounding = px(5.0f);
    style.TabRounding = px(5.0f);
    style.WindowBorderSize = px(1.0f);
    style.ChildBorderSize = px(1.0f);
    style.FrameBorderSize = px(1.0f);
    style.PopupBorderSize = px(1.0f);

    const auto window = ui::rgb(0x14171c);
    const auto panel = ui::rgb(0x1a1e25);
    const auto raised = ui::rgb(0x20252d);
    const auto border = ui::rgb(0x2a303a);
    const auto top = ui::rgb(0x0f1216);
    const auto text = ui::rgb(0xe6e9ee);
    const auto secondary = ui::rgb(0x9aa3b0);
    const auto accent = ui::rgb(0x3fb4e6);
    const auto selected = ui::rgb(0x1f3a4a);

    style.Colors[ImGuiCol_Text] = text;
    style.Colors[ImGuiCol_TextDisabled] = secondary;
    style.Colors[ImGuiCol_WindowBg] = panel;
    style.Colors[ImGuiCol_ChildBg] = window;
    style.Colors[ImGuiCol_PopupBg] = raised;
    style.Colors[ImGuiCol_Border] = border;
    style.Colors[ImGuiCol_BorderShadow] = ui::rgb(0x000000, 0.35f);
    style.Colors[ImGuiCol_TitleBg] = top;
    style.Colors[ImGuiCol_TitleBgActive] = top;
    style.Colors[ImGuiCol_TitleBgCollapsed] = top;
    style.Colors[ImGuiCol_MenuBarBg] = top;
    style.Colors[ImGuiCol_Header] = raised;
    style.Colors[ImGuiCol_HeaderHovered] = ui::rgb(0x2a303a);
    style.Colors[ImGuiCol_HeaderActive] = selected;
    style.Colors[ImGuiCol_Button] = ui::rgb(0x2a303a);
    style.Colors[ImGuiCol_ButtonHovered] = ui::rgb(0x343c48);
    style.Colors[ImGuiCol_ButtonActive] = ui::rgb(0x3a4350);
    style.Colors[ImGuiCol_FrameBg] = ui::rgb(0x14171c);
    style.Colors[ImGuiCol_FrameBgHovered] = ui::rgb(0x20252d);
    style.Colors[ImGuiCol_FrameBgActive] = selected;
    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = ui::rgb(0x3a8fb8);
    style.Colors[ImGuiCol_SliderGrabActive] = accent;
    style.Colors[ImGuiCol_Tab] = top;
    style.Colors[ImGuiCol_TabHovered] = ui::rgb(0x2a303a);
    style.Colors[ImGuiCol_TabActive] = selected;
    style.Colors[ImGuiCol_TabUnfocused] = top;
    style.Colors[ImGuiCol_TabUnfocusedActive] = ui::rgb(0x182c38);
    style.Colors[ImGuiCol_TableHeaderBg] = raised;
    style.Colors[ImGuiCol_TableBorderStrong] = border;
    style.Colors[ImGuiCol_TableBorderLight] = ui::rgb(0x232831);
    style.Colors[ImGuiCol_TableRowBg] = panel;
    style.Colors[ImGuiCol_TableRowBgAlt] = ui::rgb(0x1d222a);
    style.Colors[ImGuiCol_Separator] = border;
    style.Colors[ImGuiCol_SeparatorHovered] = accent;
    style.Colors[ImGuiCol_SeparatorActive] = accent;
    style.Colors[ImGuiCol_DockingPreview] = ui::rgb(0x3fb4e6, 0.52f);
    style.Colors[ImGuiCol_DockingEmptyBg] = window;
    style.Colors[ImGuiCol_NavHighlight] = accent;
}
}
