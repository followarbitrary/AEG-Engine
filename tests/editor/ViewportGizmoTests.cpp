#include <ViewportGizmo.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace aeg;
using namespace aeg::editor;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Harness {
    ImGuiContext* context{ImGui::CreateContext()};
    ViewportGizmoState gizmo;
    Transform transform;
    math::Vec3 localPivot{0};
    camera::OrbitCamera camera;
    ViewportRect rect{{0, 0}, {800, 600}};
    std::vector<ImDrawVert> gizmoVertices;
    bool allowInput{true};
    bool toolbar{false};
    bool allowShortcuts{true};
    Harness() {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {800, 600};
        io.DeltaTime = 1.0f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        camera.fit({{-1, -1, -1}, {1, 1, 1}});
        frame(-100, -100);
    }
    ~Harness() {
        cancelTransformGizmo(gizmo, transform, false);
        ImGui::DestroyContext(context);
    }
    bool frame(float x, float y, bool down = false) {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        io.AddMouseButtonEvent(0, down);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({800, 600});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::Begin("Viewport fixture", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        bool claimed = toolbar && drawGizmoToolbar(gizmo, rect, allowShortcuts);
        auto* draw = ImGui::GetWindowDrawList();
        const int startVertex = draw->VtxBuffer.Size;
        claimed |= drawTransformGizmo(gizmo, transform, camera, localPivot, rect, allowInput && !claimed);
        gizmoVertices.assign(draw->VtxBuffer.Data + startVertex, draw->VtxBuffer.Data + draw->VtxBuffer.Size);
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::Render();
        return claimed;
    }
};

math::Vec2 screenPoint(const Harness& h, math::Vec3 world, math::Vec2 framebufferScale = {1, 1}) {
    const float aspect = h.rect.size.x * framebufferScale.x / (h.rect.size.y * framebufferScale.y);
    const auto clip = h.camera.projection(aspect, {true, false}) * h.camera.view() * math::Vec4(world, 1);
    require(clip.w > 0, "Expected screen point must be in front of the camera");
    return {h.rect.position.x + (clip.x / clip.w * 0.5f + 0.5f) * h.rect.size.x,
            h.rect.position.y + (clip.y / clip.w * 0.5f + 0.5f) * h.rect.size.y};
}

bool axisColor(ImU32 color, unsigned axis) {
    const unsigned channels[]{(color >> IM_COL32_R_SHIFT) & 255, (color >> IM_COL32_G_SHIFT) & 255,
                              (color >> IM_COL32_B_SHIFT) & 255};
    return ((color >> IM_COL32_A_SHIFT) & 255) > 240 && channels[axis] > 100 &&
           channels[axis] > channels[(axis + 1) % 3] + 64 && channels[axis] > channels[(axis + 2) % 3] + 64;
}

math::Vec2 drawnCenter(const Harness& h) {
    math::Vec2 minimum{std::numeric_limits<float>::max()}, maximum{-std::numeric_limits<float>::max()};
    unsigned count = 0;
    for (const auto& vertex : h.gizmoVertices) {
        const auto color = vertex.col;
        if (((color >> IM_COL32_A_SHIFT) & 255) < 240 || ((color >> IM_COL32_R_SHIFT) & 255) < 240 ||
            ((color >> IM_COL32_G_SHIFT) & 255) < 240 || ((color >> IM_COL32_B_SHIFT) & 255) < 240)
            continue;
        minimum = math::min(minimum, {vertex.pos.x, vertex.pos.y});
        maximum = math::max(maximum, {vertex.pos.x, vertex.pos.y});
        ++count;
    }
    require(count > 3, "Observe the drawn white center/ring before selecting a handle");
    return (minimum + maximum) * 0.5f;
}

float observedRingRadius(const Harness& h, math::Vec2 center) {
    std::vector<float> radii;
    for (const auto& vertex : h.gizmoVertices)
        if (axisColor(vertex.col, 2)) radii.push_back(math::length(math::Vec2{vertex.pos.x, vertex.pos.y} - center));
    require(radii.size() > 16, "Observe an actual blue Z rotation ring before dragging it");
    std::sort(radii.begin(), radii.end());
    return radii[radii.size() / 2];
}

math::Vec2 observedAxisPoint(const Harness& h, unsigned axis, math::Vec2 center, math::Vec2 direction) {
    direction = math::normalize(direction);
    float extent = 0;
    for (const auto& vertex : h.gizmoVertices) {
        if (!axisColor(vertex.col, axis)) continue;
        const auto offset = math::Vec2{vertex.pos.x, vertex.pos.y} - center;
        const float along = math::dot(offset, direction);
        const float perpendicular = std::abs(offset.x * direction.y - offset.y * direction.x);
        if (perpendicular < 8) extent = std::max(extent, along);
    }
    require(extent > 25, "Observe an actual colored axis before selecting a point on its handle");
    return center + direction * (extent * 0.65f);
}

bool sameTransform(const Transform& a, const Transform& b) {
    return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}
void axisDragFollowsScreenAndPreservesCamera() {
    Harness h;
    const auto originalView = h.camera.view();
    h.frame(440, 300);
    require(h.frame(440, 300, true) && h.gizmo.active, "X handle must claim the gesture before camera orbit");
    h.frame(480, 300, true);
    require(h.transform.translation.x > 0.1f && std::abs(h.transform.translation.y) < 0.001f &&
                std::abs(h.transform.translation.z) < 0.001f,
            "Dragging the front-view X handle right moves only world X right");
    require(math::nearly_equal(h.camera.view(), originalView, 0.00001f), "Transform dragging must preserve camera");
    h.frame(480, 300, false);
    require(!h.gizmo.active, "Mouse release finishes the transform transaction");
    Harness vertical;
    vertical.frame(400, 260);
    vertical.frame(400, 260, true);
    vertical.frame(400, 220, true);
    require(vertical.transform.translation.y > 0.1f && std::abs(vertical.transform.translation.x) < 0.001f,
            "Screen-up Y dragging increases world Y despite Vulkan Y-down rendering");
    vertical.frame(400, 220, false);
}
void escapeRestoresAndBlocksUntilRelease() {
    Harness h;
    h.frame(440, 300);
    h.frame(440, 300, true);
    h.frame(480, 300, true);
    require(h.transform.translation.x > 0.1f, "Escape fixture must perform an actual transform drag");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    h.frame(490, 300, true);
    require(h.transform.translation == math::Vec3{0} && !h.gizmo.active && h.gizmo.blockedUntilRelease,
            "Escape restores drag-start transform and prevents camera handoff until release");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    h.frame(510, 300, true);
    require(h.transform.translation == math::Vec3{0}, "Held mouse after Escape cannot restart manipulation");
    h.frame(510, 300, false);
    require(!h.gizmo.blockedUntilRelease, "Release clears cancelled gesture ownership");
}
void outsideReleaseAndDisabledInput() {
    Harness h;
    h.frame(440, 300);
    h.frame(440, 300, true);
    require(h.frame(850, 300, true) && h.gizmo.active, "A handle drag keeps ownership outside the viewport");
    h.frame(850, 300, false);
    require(!h.gizmo.active, "Release outside the viewport must finish the handle drag");
    Harness disabled;
    disabled.allowInput = false;
    disabled.frame(440, 300);
    disabled.frame(440, 300, true);
    disabled.frame(480, 300, true);
    require(!disabled.gizmo.active && disabled.transform.translation == math::Vec3{0},
            "An existing camera/overlay gesture prevents transform capture");
    disabled.frame(480, 300, false);
}
void shortcutsRespectFocusAndTextEditing() {
    Harness h;
    h.toolbar = true;
    ImGui::GetIO().AddKeyEvent(ImGuiKey_E, true);
    h.frame(600, 500);
    require(h.gizmo.operation == GizmoOperation::Rotate, "E selects rotate in focused viewport");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_E, false);
    h.frame(600, 500);
    h.allowShortcuts = false;
    ImGui::GetIO().AddKeyEvent(ImGuiKey_R, true);
    h.frame(600, 500);
    require(h.gizmo.operation == GizmoOperation::Rotate, "Typing or unfocused viewport must suppress R shortcut");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_R, false);
    h.frame(600, 500);
}

void rotationFollowsPointerAroundTheModelCenter() {
    Harness h;
    h.gizmo.operation = GizmoOperation::Rotate;
    h.localPivot = {1, 2, 0};
    h.transform.translation = {-1, -2, 0};
    const auto originalCamera = h.camera.view();
    h.frame(700, 500);
    const auto center = drawnCenter(h);
    require(math::nearly_equal(center, math::Vec2{400, 300}, 0.5f),
            "Offset model center must anchor the rotation ring");
    const float diagonal = observedRingRadius(h, center) / std::sqrt(2.0f);
    h.frame(center.x + diagonal, center.y - diagonal);
    h.frame(center.x + diagonal, center.y - diagonal, true);
    require(h.gizmo.active, "The observed Z ring must begin a real rotation transaction");
    h.frame(center.x - diagonal, center.y - diagonal, true);
    const auto model = h.transform.matrix();
    require(math::nearly_equal(math::transform_point(model, h.localPivot), math::Vec3{0}, 0.002f),
            "Rotation must keep the model center fixed instead of orbiting around its origin");
    require(math::nearly_equal(math::transform_point(model, {2, 2, 0}), math::Vec3{0, 1, 0}, 0.03f),
            "Counterclockwise quarter-circle dragging must move the world-right marker to world up");
    require(math::nearly_equal(h.transform.translation, math::Vec3{2, -1, 0}, 0.03f) &&
                h.transform.scale == math::Vec3{1},
            "Centered rotation must compensate model translation and preserve scale");
    require(math::nearly_equal(h.camera.view(), originalCamera, 0.00001f), "Rotation handles must preserve camera");
    h.frame(center.x - diagonal, center.y - diagonal, false);
    require(!h.gizmo.active, "Releasing the rotation ring finishes the transaction");
}

void localScalePreservesReflectionRotationAndCenter() {
    Harness h;
    h.gizmo.operation = GizmoOperation::Scale;
    h.gizmo.space = GizmoSpace::World;
    h.localPivot = {1, 2, -0.5f};
    const float root3 = std::sqrt(3.0f);
    h.transform = {{root3 + 3, 1 - 3 * root3, 2}, {0, 0, 30}, {-2, 3, 4}};
    const auto originalRotation = h.transform.rotation;
    const auto originalCamera = h.camera.view();
    h.frame(700, 500);
    const auto center = drawnCenter(h);
    const auto positiveDirection = screenPoint(h, {root3 * 0.5f, 0.5f, 0}) - center;
    // The displayed red axis may use either reflection convention. Observe its
    // actual extent, then drag away from the center along that same visible axis.
    math::Vec2 redOffset;
    for (const auto& vertex : h.gizmoVertices)
        if (axisColor(vertex.col, 0)) {
            const auto offset = math::Vec2{vertex.pos.x, vertex.pos.y} - center;
            if (math::length_squared(offset) > math::length_squared(redOffset)) redOffset = offset;
        }
    require(std::abs(math::dot(math::normalize(redOffset), math::normalize(positiveDirection))) > 0.98f,
            "Scale handle must align with the rotated local X axis, even when World mode was selected");
    const auto handle = observedAxisPoint(h, 0, center, redOffset);
    h.frame(handle.x, handle.y);
    h.frame(handle.x, handle.y, true);
    require(h.gizmo.active, "The observed red scale handle must begin scaling");
    const auto farther = center + (handle - center) * 1.7f;
    h.frame(farther.x, farther.y, true);
    require(h.transform.scale.x < -2.2f && std::abs(h.transform.scale.y - 3) < 0.001f &&
                std::abs(h.transform.scale.z - 4) < 0.001f,
            "Local X scaling changes only its signed magnitude while preserving other scale components");
    require(h.transform.rotation == originalRotation &&
                math::nearly_equal(math::transform_point(h.transform.matrix(), h.localPivot), math::Vec3{0}, 0.003f),
            "Nonuniform reflected scaling preserves rotation and the selected model center");
    require(math::nearly_equal(h.camera.view(), originalCamera, 0.00001f), "Scale handles must preserve camera");
    h.frame(farther.x, farther.y, false);
    require(!h.gizmo.active, "Releasing the scale handle finishes the transaction");
}

void reflectedRotationFollowsTheSamePointerDirection() {
    for (const float sign : {-1.0f, 1.0f}) {
        Harness h;
        h.gizmo.operation = GizmoOperation::Rotate;
        h.transform.scale = {sign, 1, 1};
        h.frame(700, 500);
        const auto center = drawnCenter(h);
        const float diagonal = observedRingRadius(h, center) / std::sqrt(2.0f);
        h.frame(center.x + diagonal, center.y - diagonal);
        h.frame(center.x + diagonal, center.y - diagonal, true);
        require(h.gizmo.active, "A reflected model must still expose an editable local Z ring");
        h.frame(center.x - diagonal, center.y - diagonal, true);
        // For the mirrored model, local -X is the same initial world-right marker.
        const auto marker = math::transform_point(h.transform.matrix(), {sign, 0, 0});
        require(math::nearly_equal(marker, math::Vec3{0, 1, 0}, 0.03f),
                "Reflection must not reverse rotation relative to the pointer's counterclockwise arc");
        require(h.transform.scale == math::Vec3{sign, 1, 1}, "Rotation must preserve reflection signs");
        h.frame(center.x - diagonal, center.y - diagonal, false);
    }
}

void rotationAndScaleContinueAndReleaseOutsideTheWindow() {
    for (const auto operation : {GizmoOperation::Rotate, GizmoOperation::Scale}) {
        Harness h;
        h.gizmo.operation = operation;
        h.frame(700, 500);
        const auto center = drawnCenter(h);
        if (operation == GizmoOperation::Rotate) {
            const float diagonal = observedRingRadius(h, center) / std::sqrt(2.0f);
            h.frame(center.x + diagonal, center.y - diagonal);
            h.frame(center.x + diagonal, center.y - diagonal, true);
            h.frame(center.x, center.y - diagonal * std::sqrt(2.0f), true);
        } else {
            h.frame(center.x, center.y);
            h.frame(center.x, center.y, true);
            h.frame(center.x + 30, center.y, true);
        }
        require(h.gizmo.active, "Outside-release fixture must first start a real rotate/scale drag");
        const auto inside = h.transform;
        require(h.frame(850, 100, true) && h.gizmo.active, "Rotate and scale retain ownership outside their window");
        require(!sameTransform(h.transform, inside),
                "Rotate and scale keep applying pointer motion outside the window");
        h.frame(850, 100, false);
        require(!h.gizmo.active, "Mouse release outside the window must end both rotate and scale transactions");
        const auto released = h.transform;
        h.frame(700, 500);
        require(sameTransform(h.transform, released),
                "Reentering after outside release must not continue a stale drag");
    }
}

void worldAxesWorkWithRotatedCameraAndObject() {
    Harness h;
    require(h.camera.setViewDirection({1, 1, 1}), "Set an oblique camera for world-space manipulation");
    h.gizmo.space = GizmoSpace::World;
    h.transform.rotation = {25, 35, 45};
    const auto original = h.transform;
    const auto originalView = h.camera.view();
    h.frame(700, 500);
    const auto center = drawnCenter(h);
    const auto direction = math::normalize(screenPoint(h, {1, 0, 0}) - center);
    const auto handle = observedAxisPoint(h, 0, center, direction);
    h.frame(handle.x, handle.y);
    h.frame(handle.x, handle.y, true);
    require(h.gizmo.active, "The projected world X handle must be selectable from an oblique camera");
    const auto farther = handle + direction * 40;
    h.frame(farther.x, farther.y, true);
    require(h.transform.translation.x > 0.1f && std::abs(h.transform.translation.y) < 0.002f &&
                std::abs(h.transform.translation.z) < 0.002f,
            "Oblique world-X dragging must move only world X, not the object's rotated local axis");
    require(h.transform.rotation == original.rotation && h.transform.scale == original.scale &&
                math::nearly_equal(h.camera.view(), originalView, 0.00001f),
            "World translation preserves object orientation, scale and camera");
    h.frame(farther.x, farther.y, false);
}

void hiddenBehindCameraCannotClaimInput() {
    Harness h;
    h.transform.translation = {0, 0, h.camera.distance() * 2};
    const auto original = h.transform;
    h.frame(400, 300);
    require(h.gizmoVertices.empty(), "A behind-camera object must not emit transform handles");
    h.frame(400, 300, true);
    h.frame(450, 250, true);
    require(!h.gizmo.active && !h.gizmo.hovered && sameTransform(h.transform, original),
            "Behind-camera handles must not capture input or edit the model");
    h.frame(450, 250, false);
}

void anisotropicDpiKeepsTheOverlayRegisteredToRenderedPixels() {
    Harness h;
    ImGui::GetIO().DisplayFramebufferScale = {2, 1.5f};
    h.transform.translation = {1, 0.5f, 0};
    h.frame(700, 500);
    const auto expected = screenPoint(h, {1, 0.5f, 0}, {2, 1.5f});
    require(math::nearly_equal(drawnCenter(h), expected, 0.5f),
            "Gizmo center must match the rendered pixel-aspect camera when framebuffer X/Y scales differ");
}
}
int main() {
    int failures = 0;
    const std::pair<const char*, std::function<void()>> tests[]{
        {"screen-axis transform drag", axisDragFollowsScreenAndPreservesCamera},
        {"Escape transaction", escapeRestoresAndBlocksUntilRelease},
        {"outside release and disabled capture", outsideReleaseAndDisabledInput},
        {"viewport shortcuts", shortcutsRespectFocusAndTextEditing},
        {"rotation follows pointer around selected center", rotationFollowsPointerAroundTheModelCenter},
        {"signed nonuniform local scale", localScalePreservesReflectionRotationAndCenter},
        {"reflected rotation follows pointer", reflectedRotationFollowsTheSamePointerDirection},
        {"rotate and scale outside-window release", rotationAndScaleContinueAndReleaseOutsideTheWindow},
        {"world axis with oblique camera", worldAxesWorkWithRotatedCameraAndObject},
        {"behind-camera input rejection", hiddenBehindCameraCannotClaimInput},
        {"anisotropic framebuffer overlay registration", anisotropicDpiKeepsTheOverlayRegisteredToRenderedPixels}};
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
