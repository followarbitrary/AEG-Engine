#include "ViewportGizmo.hpp"
#include <imgui.h>
#include <ImGuizmo.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace aeg::editor {
namespace {
std::array<float, 16> matrixData(const math::Mat4& matrix) {
    std::array<float, 16> result;
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row) result[column * 4 + row] = matrix.at(row, column);
    return result;
}
math::Mat4 fromData(const std::array<float, 16>& values) {
    math::Mat4 result;
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row) result.at(row, column) = values[column * 4 + row];
    return result;
}
ImGuizmo::OPERATION operation(GizmoOperation value) {
    switch (value) {
    case GizmoOperation::Rotate:
        return ImGuizmo::ROTATE;
    case GizmoOperation::Scale:
        return ImGuizmo::SCALE;
    default:
        return ImGuizmo::TRANSLATE;
    }
}
}

bool drawGizmoToolbar(ViewportGizmoState& state, ViewportRect rect, bool allowShortcuts) {
    auto& io = ImGui::GetIO();
    if (allowShortcuts && !io.WantTextInput && !ImGui::IsAnyItemActive() && !state.active &&
        !state.blockedUntilRelease && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) state.operation = GizmoOperation::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) state.operation = GizmoOperation::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) state.operation = GizmoOperation::Scale;
    }
    if (rect.size.x < 180 || rect.size.y < 44) return false;
    const auto previous = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect({rect.position.x, rect.position.y},
                       {rect.position.x + rect.size.x, rect.position.y + rect.size.y}, true);
    ImGui::SetCursorScreenPos({rect.position.x + 8, rect.position.y + 8});
    ImGui::PushID("Viewport transform tools");
    ImGui::BeginGroup();
    ImGui::BeginDisabled(state.active || state.blockedUntilRelease);
    constexpr const char* labels[]{"移动 W", "旋转 E", "缩放 R"};
    constexpr const char* compactLabels[]{"W", "E", "R"};
    const bool compact = rect.size.x < 320;
    bool hovered = false;
    for (int index = 0; index < 3; ++index) {
        if (index && !compact) ImGui::SameLine(0, 3);
        const bool selected = state.operation == static_cast<GizmoOperation>(index);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(compact ? compactLabels[index] : labels[index], compact ? ImVec2(28, 0) : ImVec2(0, 0)))
            state.operation = static_cast<GizmoOperation>(index);
        if (selected) ImGui::PopStyleColor();
        hovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemActive();
        if (compact && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", labels[index]);
    }
    // Local scaling keeps nonuniform TRS representable without introducing shear.
    if (rect.size.x >= 450) {
        ImGui::SameLine(0, 6);
        ImGui::BeginDisabled(state.operation == GizmoOperation::Scale);
        if (ImGui::Button(state.operation == GizmoOperation::Scale || state.space == GizmoSpace::Local ? "局部"
                                                                                                       : "世界"))
            state.space = state.space == GizmoSpace::Local ? GizmoSpace::World : GizmoSpace::Local;
        hovered |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemActive();
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(previous);
    draw->PopClipRect();
    return hovered;
}

void cancelTransformGizmo(ViewportGizmoState& state, Transform& transform, bool restore) {
    if (restore && state.active) transform = state.dragStart;
    state.active = false;
    state.hovered = false;
    state.blockedUntilRelease = ImGui::GetCurrentContext() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    ImGuizmo::Enable(false);
}

bool drawTransformGizmo(ViewportGizmoState& state, Transform& transform, const camera::OrbitCamera& camera,
                        math::Vec3 localPivot, ViewportRect rect, bool allowInput) {
    auto& io = ImGui::GetIO();
    if (state.active && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        cancelTransformGizmo(state, transform, true);
        return true;
    }
    if (state.blockedUntilRelease) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) state.blockedUntilRelease = false;
        return true;
    }
    state.hovered = false;
    if (rect.size.x < 32 || rect.size.y < 32 || !math::is_finite(rect.size) || !math::is_finite(rect.position)) {
        cancelTransformGizmo(state, transform, true);
        return false;
    }
    const auto view = camera.view();
    // ImGuizmo maps NDC +Y to screen up. The rendered Vulkan image uses Y-down.
    const float aspect = rect.size.x * io.DisplayFramebufferScale.x / (rect.size.y * io.DisplayFramebufferScale.y);
    const auto projection = camera.projection(aspect, {false, false});
    auto currentMatrix = state.active ? state.dragMatrix : pivotMatrix(transform, localPivot);
    // ImGuizmo's local rotation assumes a right-handed basis. Reflect only the
    // model, keeping handle orientation independent of the signs of its scale.
    // The world-space center is unchanged; restore signs before TRS recovery.
    if (!state.active)
        for (std::size_t axis = 0; axis < 3; ++axis)
            if (transform.scale[axis] < 0) currentMatrix[axis] = -currentMatrix[axis];
    const auto clip = projection * view * currentMatrix[3];
    if (!math::is_finite(currentMatrix) || !math::is_finite(clip) || clip.w <= 0 ||
        (!state.active && (clip.z < 0 || clip.z > clip.w))) {
        cancelTransformGizmo(state, transform, true);
        return false;
    }
    auto viewValues = matrixData(view), projectionValues = matrixData(projection),
         matrixValues = matrixData(currentMatrix);
    ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(rect.position.x, rect.position.y, rect.size.x, rect.size.y);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetGizmoSizeClipSpace(0.12f);
    ImGuizmo::AllowAxisFlip(false);
    const bool canStart = allowInput && !io.WantTextInput && rect.contains({io.MousePos.x, io.MousePos.y}) &&
                          ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGuizmo::Enable(state.active || canStart);
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect({rect.position.x, rect.position.y},
                       {rect.position.x + rect.size.x, rect.position.y + rect.size.y}, true);
    ImGuizmo::PushID("Selected model");
    const auto before = transform;
    const bool wasActive = state.active;
    const auto mode = state.operation == GizmoOperation::Scale || state.space == GizmoSpace::Local ? ImGuizmo::LOCAL
                                                                                                   : ImGuizmo::WORLD;
    const bool changed = ImGuizmo::Manipulate(viewValues.data(), projectionValues.data(), operation(state.operation),
                                              mode, matrixValues.data());
    const bool usingGizmo = ImGuizmo::IsUsing();
    state.hovered = canStart && ImGuizmo::IsOver(operation(state.operation));
    ImGuizmo::PopID();
    draw->PopClipRect();
    if (!wasActive && usingGizmo) state.dragStart = before;
    if (changed || usingGizmo) state.dragMatrix = fromData(matrixValues);
    if (changed) {
        auto candidateMatrix = fromData(matrixValues);
        for (std::size_t axis = 0; axis < 3; ++axis)
            if (state.dragStart.scale[axis] < 0) candidateMatrix[axis] = -candidateMatrix[axis];
        Transform candidate;
        if (tryApplyPivotMatrix(candidateMatrix, localPivot, transform, candidate)) {
            if (state.operation == GizmoOperation::Translate) {
                candidate.rotation = state.dragStart.rotation;
                candidate.scale = state.dragStart.scale;
            }
            if (state.operation == GizmoOperation::Rotate) candidate.scale = state.dragStart.scale;
            if (state.operation == GizmoOperation::Scale) candidate.rotation = state.dragStart.rotation;
            transform = candidate;
        }
    }
    state.active = usingGizmo;
    return state.hovered || state.active || wasActive;
}
}
