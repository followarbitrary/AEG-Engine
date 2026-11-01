#include "EditorState.hpp"

#include <algorithm>
#include <exception>
#include <string>

namespace aeg::editor {
namespace {
void drawDebugHeading(const char* label, const char* detail = nullptr) {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (detail) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", detail);
    }
    ImGui::Spacing();
}
}

void drawDebugContents(EditorState& state, render::Renderer& renderer, EditorGraphics& graphics) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
    drawDebugHeading("RenderGraph pass", "Pass visibility");
    auto passes = renderer.renderDebugSettings();
    bool passChanged = false;
    passChanged |= ImGui::Checkbox("阴影 pass###Debug shadows", &passes.shadows);
    ImGui::BeginDisabled(!renderer.supportsTemporalAA());
    passChanged |= ImGui::Checkbox("TAA pass###Debug taa", &passes.taa);
    ImGui::EndDisabled();
    passChanged |= ImGui::Checkbox("色调映射 pass###Debug tone mapping", &passes.toneMapping);
    passChanged |= ImGui::Checkbox("FXAA pass###Debug fxaa", &passes.fxaa);
    if (passChanged) {
        renderer.setRenderDebugSettings(passes);
        state.debugPasses = passes;
    }
    ImGui::TextDisabled("Scene 与 Present 始终保留。");

    drawDebugHeading("TAA 参数", "Temporal accumulation");
    auto aa = renderer.antialiasing();
    bool taaChanged = false;
    ImGui::BeginDisabled(!renderer.supportsTemporalAA());
    taaChanged |= ImGui::SliderFloat("历史权重###TAA history weight", &aa.taaHistoryWeight, 0.0f, 0.9f, "%.3f");
    taaChanged |= ImGui::SliderFloat("深度阈值###TAA depth threshold", &aa.taaDepthThreshold, 0.0f, 0.25f, "%.4f");
    ImGui::EndDisabled();
    if (taaChanged) {
        try {
            renderer.setAntialiasing(aa);
            state.antialiasing = renderer.antialiasing();
        } catch (const std::exception& error) {
            state.antialiasingNotice = std::string("TAA 参数无效：") + error.what();
        }
    }
    if (renderer.antialiasing().taa)
        ImGui::Text("历史帧：%u", renderer.temporalFrameCount());
    if (ui::SecondaryButton("重置 TAA 历史###Debug reset TAA")) renderer.resetTemporalHistory();

    drawDebugHeading("上一帧 GPU 时间戳", "Pass timings");
    const auto timings = graphics.gpuPassTimings();
    if (timings.empty()) {
        ImGui::TextDisabled("暂无可用 query 结果。");
    } else {
        for (const auto& timing : timings) {
            if (timing.valid)
                ImGui::Text("%s  %.3f ms", timing.name.c_str(), timing.milliseconds);
            else
                ImGui::TextDisabled("%s  --", timing.name.c_str());
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("RenderGraph dump###Debug graph dump")) {
        const auto& dump = graphics.renderGraphDump();
        if (!dump.empty() && ui::SecondaryButton("复制 dump###Debug copy graph dump"))
            ImGui::SetClipboardText(dump.c_str());
        ImGui::BeginChild("##GraphDump", ImVec2(0, 180), true);
        std::string text = dump.empty() ? "暂无 graph dump。" : dump;
        ImGui::InputTextMultiline("##GraphDumpText", text.data(), text.size() + 1, ImVec2(-1, -1),
                                  ImGuiInputTextFlags_ReadOnly);
        ImGui::EndChild();
    }
    ImGui::PopStyleVar(2);
}
}
