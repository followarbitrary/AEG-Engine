#include "EditorState.hpp"

namespace aeg::editor {
void configureStyle() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {14, 12};
    style.FramePadding = {8, 6};
    style.ItemSpacing = {8, 8};
    style.WindowRounding = 0;
    style.ChildRounding = 5;
    style.FrameRounding = 4;
    style.PopupRounding = 5;
    style.GrabRounding = 3;
    style.TabRounding = 4;
    style.WindowBorderSize = 0;
    style.FrameBorderSize = 0;
    style.Colors[ImGuiCol_WindowBg] = {0.067f, 0.078f, 0.098f, 1};
    style.Colors[ImGuiCol_ChildBg] = {0.055f, 0.063f, 0.080f, 1};
    style.Colors[ImGuiCol_TitleBg] = {0.045f, 0.055f, 0.070f, 1};
    style.Colors[ImGuiCol_TitleBgActive] = {0.09f, 0.12f, 0.16f, 1};
    style.Colors[ImGuiCol_Header] = {0.12f, 0.24f, 0.32f, 1};
    style.Colors[ImGuiCol_HeaderHovered] = {0.16f, 0.32f, 0.43f, 1};
    style.Colors[ImGuiCol_Button] = {0.13f, 0.28f, 0.38f, 1};
    style.Colors[ImGuiCol_ButtonHovered] = {0.18f, 0.40f, 0.54f, 1};
    style.Colors[ImGuiCol_CheckMark] = {0.33f, 0.77f, 0.94f, 1};
    style.Colors[ImGuiCol_SliderGrab] = {0.27f, 0.65f, 0.83f, 1};
    style.Colors[ImGuiCol_DockingPreview] = {0.25f, 0.60f, 0.85f, 0.5f};
}
}
