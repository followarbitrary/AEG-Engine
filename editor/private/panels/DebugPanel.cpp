#include "EditorState.hpp"

#include <algorithm>
#include <exception>

namespace aeg::editor {
void drawDebugPanel(EditorState& state, render::Renderer& renderer, EditorGraphics& graphics) {
    if (!ImGui::Begin(debugWindow)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("RenderGraph pass");
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

    ImGui::Separator();
    ImGui::TextDisabled("TAA 参数");
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
    if (ImGui::Button("重置 TAA 历史###Debug reset TAA")) renderer.resetTemporalHistory();

    ImGui::Separator();
    ImGui::TextDisabled("上一帧 GPU 时间戳");
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

    if (ImGui::CollapsingHeader("RenderGraph dump###Debug graph dump")) {
        ImGui::BeginChild("##GraphDump", ImVec2(0, 180), true);
        ImGui::TextUnformatted(graphics.renderGraphDump().empty() ? "暂无 graph dump。"
                                                                    : graphics.renderGraphDump().c_str());
        ImGui::EndChild();
    }
    ImGui::End();
}
}
