#include "EditorState.hpp"
#include <EnvironmentImage.hpp>
#include <Utf8Path.hpp>
#include <exception>

namespace aeg::editor {
void drawEnvironmentSection(EditorState& state, render::Renderer& renderer) {
    const auto summary = renderer.environment().enabled ? "开" : "关";
    if (!ui::SectionHeader("环境漫反射###Environment lighting", summary)) return;
    const bool supported = renderer.supportsEnvironment();
    auto candidate = renderer.environment();
    ImGui::BeginDisabled(!supported);
    bool changed = ImGui::Checkbox("开启 IBL###Enable environment", &candidate.enabled);
    ImGui::BeginDisabled(!candidate.enabled);
    changed |= ImGui::DragFloat("强度###Environment intensity", &candidate.intensity, .02f, 0, 100, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp);
    float degrees = math::to_degrees(candidate.rotationRadians);
    if (ImGui::SliderFloat("水平旋转###Environment rotation", &degrees, -180, 180, "%.0f°")) {
        candidate.rotationRadians = math::to_radians(degrees);
        changed = true;
    }
    ImGui::EndDisabled();
    if (changed) {
        try {
            renderer.setEnvironment(candidate);
            state.environmentError.clear();
            if (candidate.enabled && state.environmentNotice.empty())
                state.environmentNotice = "已生成程序天空及 IBL 贴图。";
        } catch (const std::exception& error) {
            state.environmentError = std::string("IBL 设置失败，已保留原环境：") + error.what();
        }
    }
    ImGui::TextWrapped("线性 HDR 环境图（2:1 Radiance .hdr）；更换时由 GPU 计算预处理。");
    ImGui::SetNextItemWidth(-1);
    const bool enter =
        ImGui::InputTextWithHint("##Environment path", "HDR 环境图文件路径", state.environmentPath.data(),
                                 state.environmentPath.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("加载 HDR 环境###Load environment", {-1, 0}) || enter) {
        try {
            const auto path = foundation::pathFromUtf8(state.environmentPath.data());
            const auto image = assets::loadHdrImage(path);
            renderer.uploadEnvironment(image);
            auto enabled = renderer.environment();
            enabled.enabled = true;
            renderer.setEnvironment(enabled);
            state.environmentError.clear();
            state.environmentNotice = "已加载：" + foundation::pathToUtf8(path.filename());
        } catch (const std::exception& error) {
            state.environmentError = std::string("环境图加载失败，已保留原环境：") + error.what();
        }
    }
    if (ImGui::Button("使用程序天空###Procedural environment", {-1, 0})) {
        try {
            renderer.useProceduralEnvironment();
            auto enabled = renderer.environment();
            enabled.enabled = true;
            renderer.setEnvironment(enabled);
            state.environmentError.clear();
            state.environmentNotice = "已生成程序天空及 IBL 贴图。";
        } catch (const std::exception& error) {
            state.environmentError = std::string("程序天空生成失败，已保留原环境：") + error.what();
        }
    }
    ImGui::EndDisabled();
    if (!supported) ImGui::TextWrapped("当前设备不支持 IBL 所需的计算队列、浮点 Storage Image 或 Cubemap 线性采样。");
    if (!state.environmentNotice.empty()) ImGui::TextWrapped("%s", state.environmentNotice.c_str());
    if (!state.environmentError.empty()) ImGui::TextWrapped("%s", state.environmentError.c_str());
    ImGui::TextWrapped("IBL 替代常量环境漫反射，与直接光和阴影叠加。滤波仅在环境图更换时运行。");
}
}
