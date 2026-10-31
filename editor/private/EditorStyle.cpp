#include "EditorState.hpp"

namespace aeg::editor {
void configureStyle() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    // Keep the shell dense enough for a 1280x800 workspace while preserving
    // comfortable hit targets in the editor panels.
    style.WindowPadding = {10, 8};
    style.FramePadding = {7, 5};
    style.ItemSpacing = {7, 6};
    style.ItemInnerSpacing = {5, 4};
    style.IndentSpacing = 16;
    style.WindowRounding = 0;
    style.ChildRounding = 2;
    style.FrameRounding = 2;
    style.PopupRounding = 2;
    style.GrabRounding = 2;
    style.TabRounding = 2;
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.FrameBorderSize = 1;
    style.PopupBorderSize = 1;

    const ImVec4 panel = {0.075f, 0.086f, 0.106f, 1.0f};
    const ImVec4 panelRaised = {0.094f, 0.108f, 0.133f, 1.0f};
    const ImVec4 panelDeep = {0.052f, 0.062f, 0.078f, 1.0f};
    const ImVec4 border = {0.145f, 0.170f, 0.205f, 1.0f};
    const ImVec4 accent = {0.22f, 0.60f, 0.88f, 1.0f};
    style.Colors[ImGuiCol_WindowBg] = panel;
    style.Colors[ImGuiCol_ChildBg] = panelDeep;
    style.Colors[ImGuiCol_PopupBg] = panelRaised;
    style.Colors[ImGuiCol_Border] = border;
    style.Colors[ImGuiCol_BorderShadow] = {0, 0, 0, 0.35f};
    style.Colors[ImGuiCol_TitleBg] = {0.050f, 0.059f, 0.074f, 1};
    style.Colors[ImGuiCol_TitleBgActive] = {0.10f, 0.13f, 0.18f, 1};
    style.Colors[ImGuiCol_TitleBgCollapsed] = panelDeep;
    style.Colors[ImGuiCol_MenuBarBg] = {0.047f, 0.056f, 0.071f, 1};
    style.Colors[ImGuiCol_Header] = {0.11f, 0.24f, 0.35f, 1};
    style.Colors[ImGuiCol_HeaderHovered] = {0.15f, 0.34f, 0.47f, 1};
    style.Colors[ImGuiCol_HeaderActive] = {0.18f, 0.39f, 0.53f, 1};
    style.Colors[ImGuiCol_Button] = {0.11f, 0.23f, 0.33f, 1};
    style.Colors[ImGuiCol_ButtonHovered] = {0.16f, 0.35f, 0.48f, 1};
    style.Colors[ImGuiCol_ButtonActive] = {0.20f, 0.43f, 0.58f, 1};
    style.Colors[ImGuiCol_FrameBg] = {0.045f, 0.057f, 0.073f, 1};
    style.Colors[ImGuiCol_FrameBgHovered] = {0.085f, 0.14f, 0.19f, 1};
    style.Colors[ImGuiCol_FrameBgActive] = {0.11f, 0.21f, 0.29f, 1};
    style.Colors[ImGuiCol_CheckMark] = {0.35f, 0.78f, 0.95f, 1};
    style.Colors[ImGuiCol_SliderGrab] = {0.24f, 0.63f, 0.84f, 1};
    style.Colors[ImGuiCol_SliderGrabActive] = {0.35f, 0.76f, 0.96f, 1};
    style.Colors[ImGuiCol_Tab] = {0.070f, 0.094f, 0.12f, 1};
    style.Colors[ImGuiCol_TabHovered] = {0.16f, 0.35f, 0.48f, 1};
    style.Colors[ImGuiCol_TabActive] = {0.12f, 0.27f, 0.38f, 1};
    style.Colors[ImGuiCol_TabUnfocused] = panelDeep;
    style.Colors[ImGuiCol_TabUnfocusedActive] = {0.09f, 0.18f, 0.25f, 1};
    style.Colors[ImGuiCol_TableHeaderBg] = {0.10f, 0.15f, 0.20f, 1};
    style.Colors[ImGuiCol_TableBorderStrong] = border;
    style.Colors[ImGuiCol_TableBorderLight] = {0.10f, 0.12f, 0.15f, 1};
    style.Colors[ImGuiCol_TableRowBg] = panel;
    style.Colors[ImGuiCol_TableRowBgAlt] = {0.082f, 0.095f, 0.116f, 1};
    style.Colors[ImGuiCol_Separator] = border;
    style.Colors[ImGuiCol_SeparatorHovered] = accent;
    style.Colors[ImGuiCol_SeparatorActive] = {0.35f, 0.76f, 0.96f, 1};
    style.Colors[ImGuiCol_DockingPreview] = {0.20f, 0.58f, 0.86f, 0.52f};
    style.Colors[ImGuiCol_DockingEmptyBg] = {0.035f, 0.043f, 0.055f, 1};
}
}
