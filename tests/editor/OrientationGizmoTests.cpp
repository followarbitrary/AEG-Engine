#include <gizmo/OrientationGizmo.hpp>
#include <imgui.h>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using aeg::camera::OrbitCamera;
using aeg::editor::OrientationGizmoState;
using aeg::editor::ViewportRect;
using aeg::math::Vec2;
using aeg::math::Vec3;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Fixture {
    ImGuiContext* context{ImGui::CreateContext()};
    OrbitCamera camera;
    OrientationGizmoState state;
    ViewportRect viewport{{20, 30}, {640, 480}};
    // Full-size widget: 12px inset, 124px square, 42px axis arms.
    static constexpr Vec2 center{586, 104};
    Fixture() {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {900, 700};
        io.DeltaTime = 1.0f / 60;
        io.ConfigInputTrickleEventQueue = false;
        io.Fonts->AddFontDefault();
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        camera.fit({{-2, -1, -3}, {4, 5, 7}});
        frame({0, 0});
    }
    ~Fixture() { ImGui::DestroyContext(context); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    bool frame(Vec2 position, bool allowInput = true) {
        ImGui::GetIO().AddMousePosEvent(position.x, position.y);
        ImGui::NewFrame();
        ImGui::Begin("Host");
        const bool consumed = aeg::editor::drawOrientationGizmo(state, camera, viewport, allowInput);
        ImGui::End();
        ImGui::Render();
        return consumed;
    }
    bool press(Vec2 position, bool allowInput = true) {
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        return frame(position, allowInput);
    }
    bool release(Vec2 position, bool allowInput = true) {
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        return frame(position, allowInput);
    }
    void click(Vec2 position) {
        frame(position);
        require(press(position) && state.active, "axis press owns the input");
        require(release(position) && !state.active, "axis release consumes input and ends ownership");
    }
};

void requireDirection(const OrbitCamera& camera, Vec3 expected) {
    require(aeg::math::nearly_equal(aeg::math::normalize(camera.position() - camera.target()), expected, 0.00001f),
            "axis click selects the signed world direction");
}

void axisClicksSnapAndPreserveTheOrbit() {
    const std::array cases{std::pair{Vec2{628, 104}, Vec3{1, 0, 0}}, std::pair{Vec2{544, 104}, Vec3{-1, 0, 0}},
                           std::pair{Vec2{586, 62}, Vec3{0, 1, 0}}, std::pair{Vec2{586, 146}, Vec3{0, -1, 0}}};
    for (const auto& [point, direction] : cases) {
        Fixture fixture;
        const auto target = fixture.camera.target();
        const float distance = fixture.camera.distance();
        fixture.click(point);
        requireDirection(fixture.camera, direction);
        require(fixture.camera.target() == target && fixture.camera.distance() == distance,
                "snapping preserves target and distance");
    }
}

void alignedEndpointsFlipBetweenBothSigns() {
    Fixture fixture;
    fixture.click(Fixture::center);
    requireDirection(fixture.camera, {0, 0, -1});
    fixture.click(Fixture::center);
    requireDirection(fixture.camera, {0, 0, 1});
    fixture.click({586, 62});
    requireDirection(fixture.camera, {0, 1, 0});
    fixture.click(Fixture::center);
    requireDirection(fixture.camera, {0, -1, 0});
    fixture.click(Fixture::center);
    requireDirection(fixture.camera, {0, 1, 0});
    require(aeg::math::is_finite(fixture.camera.view()), "pole snaps leave a finite view basis");
}

void dragUsesCameraConventionAndOwnsReleaseOutside() {
    Fixture fixture;
    auto expected = fixture.camera;
    fixture.frame(Fixture::center);
    require(fixture.press(Fixture::center), "press in widget consumes camera input");
    expected.orbit(20, 12);
    require(fixture.frame({606, 116}) && fixture.state.active && fixture.state.dragged,
            "drag owns the widget and crosses the drag threshold");
    require(aeg::math::nearly_equal(fixture.camera.position(), expected.position(), 0.00001f),
            "orientation drag uses the ordinary camera orbit convention");
    expected.orbit(194, 484);
    require(fixture.frame({800, 600}) && fixture.state.active, "drag retains ownership outside the viewport");
    require(aeg::math::nearly_equal(fixture.camera.position(), expected.position(), 0.00001f),
            "outside drag continues orbiting");
    require(fixture.release({800, 600}) && !fixture.state.active, "outside release ends ownership");
    require(aeg::math::nearly_equal(fixture.camera.position(), expected.position(), 0.00001f),
            "drag release does not perform an axis snap");
    require(!fixture.frame({800, 600}), "outside pointer no longer consumes camera input after release");
}

void escapeRestoresTheDragStartCamera() {
    Fixture fixture;
    const auto original = fixture.camera;
    fixture.frame(Fixture::center);
    fixture.press(Fixture::center);
    fixture.frame({610, 120});
    require(!aeg::math::nearly_equal(fixture.camera.position(), original.position(), 0.00001f),
            "test gesture changes the camera before cancel");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    require(fixture.frame({610, 120}) && !fixture.state.active, "Escape consumes and cancels the gesture");
    require(aeg::math::nearly_equal(fixture.camera.position(), original.position(), 0.00001f) &&
                fixture.camera.target() == original.target() && fixture.camera.distance() == original.distance(),
            "Escape restores the complete drag-start camera");
    fixture.release({610, 120});
}

void blockedInputAndSmallViewportsDoNotCapture() {
    Fixture fixture;
    const auto original = fixture.camera.position();
    require(!fixture.frame(Fixture::center, false), "disabled widget does not claim hover");
    require(!fixture.press(Fixture::center, false) && !fixture.state.active,
            "disabled widget does not begin a gesture");
    fixture.release(Fixture::center, false);
    require(fixture.camera.position() == original, "disabled interaction preserves the camera");
    fixture.viewport = {{20, 30}, {36, 25}};
    require(!fixture.press({40, 42}) && !fixture.state.active, "tiny viewport hides the widget");
    fixture.release({40, 42});
    fixture.viewport = {{20, 30}, {90, 90}};
    require(!fixture.press({112, 55}) && !fixture.state.active, "clipped area outside viewport cannot capture");
    fixture.release({112, 55});
}

void disabledInputDuringAnOwnedGestureStillReleases() {
    Fixture fixture;
    fixture.frame(Fixture::center);
    require(fixture.press(Fixture::center), "begin owned gesture");
    require(fixture.frame({800, 600}, false) && fixture.state.active,
            "existing gesture retains ownership when new input is blocked");
    require(fixture.release({800, 600}, false) && !fixture.state.active,
            "existing gesture handles release while new input is blocked");
}
}

int main() {
    const std::pair<const char*, std::function<void()>> tests[]{
        {"axis clicks preserve orbit", axisClicksSnapAndPreserveTheOrbit},
        {"overlapping endpoints and exact poles", alignedEndpointsFlipBetweenBothSigns},
        {"orbit drag and outside release", dragUsesCameraConventionAndOwnsReleaseOutside},
        {"Escape restores camera", escapeRestoresTheDragStartCamera},
        {"disabled and clipped viewport input", blockedInputAndSmallViewportsDoNotCapture},
        {"owned gesture survives input blocking", disabledInputDuringAnOwnedGestureStillReleases}};
    int failures = 0;
    for (const auto& [name, run] : tests) {
        try {
            run();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
