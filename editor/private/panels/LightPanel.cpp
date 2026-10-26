#include "EditorState.hpp"
#include <ShadowSettings.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <exception>
#include <string>

namespace aeg::editor {
namespace {
constexpr std::array<const char*, 3> lightNames{"方向光", "点光源", "聚光灯"};

std::string lightLabel(const render::Light& light, std::uint32_t index) {
    const auto type = std::min(static_cast<std::size_t>(light.type), lightNames.size() - 1);
    return std::to_string(index + 1) + " · " + lightNames[type];
}

void applyShadows(EditorState& state, render::Renderer& renderer, const render::ShadowSettings& candidate) {
    try {
        renderer.setShadows(candidate);
        state.shadowError.clear();
    } catch (const std::exception& error) {
        state.shadowError = std::string("阴影设置失败，已保留原设置：") + error.what();
    }
}

void addLight(EditorState& state) {
    auto& settings = state.settings;
    if (settings.lightCount >= render::kMaxLights) return;
    auto light = render::Light{};
    light.type = static_cast<render::LightType>(state.newLightType);
    if (light.type != render::LightType::Directional) {
        const float radius =
            std::isfinite(state.model.bounds.radius()) ? std::clamp(state.model.bounds.radius(), 0.01f, 1.0e8f) : 1.0f;
        light.position = state.camera.target() + math::Vec3{1.5f, 1.5f, 1.5f} * radius;
        light.direction = math::normalize(state.camera.target() - light.position);
        light.range = std::clamp(radius * 8, 0.01f, 1.0e9f);
        light.intensity = std::clamp(3.0f * radius * radius, 0.01f, 1.0e9f);
    }
    state.selectedLight = settings.lightCount;
    settings.lights[settings.lightCount++] = light;
}

void drawLightList(EditorState& state, render::Renderer& renderer) {
    auto& settings = state.settings;
    settings.lightCount = std::min(settings.lightCount, render::kMaxLights);
    ImGui::Text("光源：%u / %u", settings.lightCount, render::kMaxLights);
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##New light type", &state.newLightType, lightNames.data(), static_cast<int>(lightNames.size()));
    ImGui::BeginDisabled(settings.lightCount >= render::kMaxLights);
    if (ImGui::Button("添加光源###Add light", {-1, 0})) addLight(state);
    ImGui::EndDisabled();
    if (!settings.lightCount) {
        ImGui::TextWrapped("没有直接光源，当前仅显示环境漫反射。");
        return;
    }
    state.selectedLight = std::min(state.selectedLight, settings.lightCount - 1);
    const auto label = lightLabel(settings.lights[state.selectedLight], state.selectedLight);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##Selected light", label.c_str())) {
        for (std::uint32_t index = 0; index < settings.lightCount; ++index) {
            const auto item = lightLabel(settings.lights[index], index);
            if (ImGui::Selectable(item.c_str(), state.selectedLight == index)) state.selectedLight = index;
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("删除所选光源###Remove light", {-1, 0})) {
        auto shadows = renderer.shadows();
        if (shadows.lightIndex == state.selectedLight)
            shadows.enabled = false;
        else if (shadows.lightIndex > state.selectedLight)
            --shadows.lightIndex;
        for (auto index = state.selectedLight; index + 1 < settings.lightCount; ++index)
            settings.lights[index] = settings.lights[index + 1];
        --settings.lightCount;
        state.selectedLight = settings.lightCount ? std::min(state.selectedLight, settings.lightCount - 1) : 0;
        applyShadows(state, renderer, shadows);
        return;
    }
    auto& light = settings.lights[state.selectedLight];
    ImGui::Checkbox("启用光源###Light enabled", &light.enabled);
    int type = static_cast<int>(light.type);
    if (ImGui::Combo("类型###Light type", &type, lightNames.data(), static_cast<int>(lightNames.size())))
        light.type = static_cast<render::LightType>(type);
    float color[]{light.color.x, light.color.y, light.color.z};
    if (ImGui::ColorEdit3("颜色###Light color", color)) light.color = {color[0], color[1], color[2]};
    ImGui::DragFloat("强度###Light intensity", &light.intensity, 0.1f, 0, 1.0e9f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    if (light.type != render::LightType::Directional) {
        float position[]{light.position.x, light.position.y, light.position.z};
        if (ImGui::DragFloat3("位置###Light position", position, 0.05f))
            light.position = {position[0], position[1], position[2]};
        ImGui::DragFloat("范围###Light range", &light.range, 0.1f, 0.01f, 1.0e9f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    }
    if (light.type != render::LightType::Point) {
        float direction[]{light.direction.x, light.direction.y, light.direction.z};
        if (ImGui::SliderFloat3("方向###Light direction", direction, -1, 1, "%.2f"))
            light.direction = {direction[0], direction[1], direction[2]};
        if (!math::is_finite(light.direction) || math::dot(light.direction, light.direction) < 0.0001f)
            light.direction = {0, -1, 0};
    }
    if (light.type == render::LightType::Spot) {
        float inner = math::to_degrees(light.innerConeAngle), outer = math::to_degrees(light.outerConeAngle);
        bool changed = ImGui::SliderFloat("内锥角###Inner cone", &inner, 0, 89, "%.1f°");
        changed |= ImGui::SliderFloat("外锥角###Outer cone", &outer, 0.1f, 89, "%.1f°");
        if (changed) {
            inner = std::min(inner, outer);
            light.innerConeAngle = math::to_radians(inner);
            light.outerConeAngle = math::to_radians(outer);
        }
    }
}

void drawShadows(EditorState& state, render::Renderer& renderer) {
    if (!ImGui::CollapsingHeader("阴影###Shadows", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (!renderer.supportsShadows()) {
        ImGui::TextWrapped("当前设备不支持可采样的深度阴影贴图。");
        return;
    }
    auto candidate = renderer.shadows();
    const auto count = std::min(state.settings.lightCount, render::kMaxLights);
    bool eligible = false;
    for (std::uint32_t index = 0; index < count; ++index)
        eligible |= state.settings.lights[index].type != render::LightType::Point;
    ImGui::BeginDisabled(!eligible && !candidate.enabled);
    bool changed = ImGui::Checkbox("开启阴影###Enable shadows", &candidate.enabled);
    ImGui::EndDisabled();
    if (changed && candidate.enabled)
        candidate.resolution = std::min(candidate.resolution, std::bit_floor(renderer.maximumShadowResolution()));
    if (changed && candidate.enabled &&
        (candidate.lightIndex >= count ||
         state.settings.lights[candidate.lightIndex].type == render::LightType::Point)) {
        for (std::uint32_t index = 0; index < count; ++index)
            if (state.settings.lights[index].type != render::LightType::Point) {
                candidate.lightIndex = index;
                break;
            }
    }
    const bool selectedValid =
        candidate.lightIndex < count && state.settings.lights[candidate.lightIndex].type != render::LightType::Point;
    const auto preview = selectedValid ? lightLabel(state.settings.lights[candidate.lightIndex], candidate.lightIndex)
                                       : std::string("请选择方向光或聚光灯");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##Shadow light", preview.c_str())) {
        for (std::uint32_t index = 0; index < count; ++index) {
            if (state.settings.lights[index].type == render::LightType::Point) continue;
            const auto label = lightLabel(state.settings.lights[index], index);
            if (ImGui::Selectable(label.c_str(), candidate.lightIndex == index)) {
                candidate.lightIndex = index;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    const auto resolutionLabel = std::to_string(candidate.resolution);
    if (ImGui::BeginCombo("分辨率###Shadow resolution", resolutionLabel.c_str())) {
        for (const auto resolution : {16u, 32u, 64u, 128u, 256u, 512u, 1024u, 2048u, 4096u}) {
            if (resolution > renderer.maximumShadowResolution()) continue;
            const auto label = std::to_string(resolution);
            if (ImGui::Selectable(label.c_str(), candidate.resolution == resolution)) {
                candidate.resolution = resolution;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    changed |= ImGui::SliderFloat("深度偏移###Shadow depth bias", &candidate.depthBias, 0, 0.02f, "%.5f");
    changed |= ImGui::SliderFloat("斜率偏移###Shadow slope bias", &candidate.slopeBias, 0, 0.05f, "%.5f");
    if (changed) applyShadows(state, renderer, candidate);
    ImGui::TextWrapped("单张阴影贴图 · 3×3 PCF。点光源的六面阴影尚未实现。");
    if (candidate.enabled && !selectedValid) ImGui::TextWrapped("当前投影光源不可用，阴影暂停。");
    if (!state.shadowError.empty()) ImGui::TextWrapped("%s", state.shadowError.c_str());
}
}

void drawLightingSection(EditorState& state, render::Renderer& renderer) {
    auto& settings = state.settings;
    if (ImGui::CollapsingHeader("光照###Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("无光照材质###Unlit materials", &settings.unlit);
        ImGui::BeginDisabled(settings.unlit);
        ImGui::Checkbox("使用法线贴图###Normal mapping", &settings.normalMapping);
        ImGui::BeginDisabled(!settings.normalMapping);
        ImGui::SliderFloat("法线强度###Normal strength", &settings.normalStrength, 0, 4, "%.2f");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        const auto normalMaps = std::count_if(state.model.materials.begin(), state.model.materials.end(),
                                              [](const auto& material) { return material.hasNormalMap; });
        ImGui::Text("使用法线贴图的材质数：%zu", static_cast<std::size_t>(normalMaps));
        ImGui::SliderFloat("环境漫反射###Ambient", &settings.ambient, 0, 1, "%.2f");
        drawLightList(state, renderer);
        float background[]{settings.background.x, settings.background.y, settings.background.z};
        if (ImGui::ColorEdit3("背景###Background", background))
            settings.background = {background[0], background[1], background[2], settings.background.w};
    }
    drawShadows(state, renderer);
}
}
