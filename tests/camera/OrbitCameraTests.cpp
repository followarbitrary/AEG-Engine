#include <Bounds.hpp>
#include <OrbitCamera.hpp>
#include <RenderCamera.hpp>
#include <math.hpp>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a - b) < 0.001f; }
bool finite(const aeg::math::Mat4& m) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(m[c][r])) return false;
    return true;
}
void defaultViewFacesForward() {
    using namespace aeg::math;
    aeg::camera::OrbitCamera camera;
    require(nearly_equal(normalize(camera.position() - camera.target()), Vec3(0, 0, 1), 0.000001f),
            "default camera observes along -Z without yaw or pitch");
    const aeg::foundation::Bounds bounds{{2, 4, -6}, {6, 12, 2}};
    camera.fit(bounds);
    require(nearly_equal(camera.position(), camera.target() + Vec3(0, 0, camera.distance()), 0.00001f),
            "initial model fitting keeps the camera directly in front of its center");
    const auto view = camera.view();
    require(nearly_equal(transform_point(view, bounds.center()), Vec3(0, 0, -camera.distance()), 0.00001f),
            "front view places the model center on the viewing axis");
    require(nearly_equal(transform_vector(view, Vec3(1, 0, 0)), Vec3(1, 0, 0), 0.000001f) &&
                nearly_equal(transform_vector(view, Vec3(0, 1, 0)), Vec3(0, 1, 0), 0.000001f),
            "front view preserves world right and up");
}
void focusPreservesOrbit() {
    using namespace aeg::math;
    aeg::camera::OrbitCamera camera;
    camera.fit({{-1, -1, -1}, {1, 1, 1}});
    camera.orbit(80, -40);
    camera.pan(20, 10, 600);
    const auto direction = normalize(camera.position() - camera.target());
    const aeg::foundation::Bounds next{{2, 4, -6}, {6, 12, 2}};
    camera.fit(next);
    require(nearly_equal(camera.target(), next.center(), 0.00001f), "focus centers the requested model");
    require(nearly_equal(normalize(camera.position() - camera.target()), direction, 0.000001f),
            "focus preserves the user's orbit angle");
}
void explicitViewDirections() {
    using namespace aeg::math;
    // Each expected screen basis is independent of the camera's Euler implementation.
    struct AxisView {
        Vec3 towardViewer;
        Vec3 right;
        Vec3 up;
    };
    const std::array views{AxisView{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, AxisView{{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                           AxisView{{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, AxisView{{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
                           AxisView{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  AxisView{{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}};
    for (const auto& axis : views) {
        aeg::camera::OrbitCamera camera;
        camera.fit({{-2, -1, -3}, {4, 5, 7}});
        camera.orbit(71, -33);
        camera.pan(12, 9, 600);
        const auto target = camera.target();
        const float distance = camera.distance();
        const auto projection = camera.projection(1.6f);
        require(camera.setViewDirection(axis.towardViewer), "accept every signed world-axis view");
        require(camera.target() == target && camera.distance() == distance,
                "view snapping preserves the orbit target and distance");
        require(camera.projection(1.6f) == projection, "view snapping preserves the fitted radius and projection");
        require(nearly_equal(camera.position(), target + axis.towardViewer * distance, 0.00001f),
                "signed axis selects the corresponding eye side of the target");
        const auto view = camera.view();
        require(finite(view) && view != Mat4{}, "all signed axis views have valid nonzero view matrices");
        require(nearly_equal(transform_point(view, target), Vec3(0, 0, -distance), 0.00001f),
                "axis view centers its target at negative camera Z");
        require(nearly_equal(transform_vector(view, axis.right), Vec3(1, 0, 0), 0.000001f) &&
                    nearly_equal(transform_vector(view, axis.up), Vec3(0, 1, 0), 0.000001f) &&
                    nearly_equal(transform_vector(view, axis.towardViewer), Vec3(0, 0, 1), 0.000001f),
                "axis views preserve the hand-derived right-handed screen basis");
        if (axis.towardViewer.y != 0)
            require(camera.position().x == target.x && camera.position().z == target.z,
                    "top and bottom views are exact poles rather than a near-pole tilt");
    }
    for (const float magnitude : {std::numeric_limits<float>::denorm_min(), 1.0f, std::numeric_limits<float>::max()}) {
        aeg::camera::OrbitCamera camera;
        require(camera.setViewDirection({0, magnitude, 0}), "normalize every nonzero finite direction scale");
        require(nearly_equal(normalize(camera.position() - camera.target()), Vec3(0, 1, 0), 0.000001f),
                "direction magnitude does not affect the selected view");
        require(camera.setViewDirection({magnitude, magnitude, magnitude}),
                "normalize finite horizontal and vertical components without float length overflow or underflow");
        require(nearly_equal(normalize(camera.position() - camera.target()), normalize(Vec3(1, 1, 1)), 0.000001f),
                "arbitrary finite direction magnitude does not affect its orientation");
    }
    aeg::camera::OrbitCamera camera;
    const Vec3 arbitrary{2, -3, -4};
    require(camera.setViewDirection(arbitrary), "accept non-axis view directions");
    require(nearly_equal(normalize(camera.position() - camera.target()), normalize(arbitrary), 0.000001f),
            "non-axis direction is normalized without reversing the view");
}
void polePanAndOrbit() {
    using namespace aeg::math;
    for (const float pole : {-1.f, 1.f}) {
        aeg::camera::OrbitCamera camera;
        require(camera.setViewDirection({0, pole, 0}), "set a pole before panning");
        const auto initialTarget = camera.target();
        const float distance = camera.distance();
        const float panUnits = 2 * distance * std::tan(kPi / 8) / 600;
        camera.pan(20, 10, 600);
        const Vec3 expectedOffset{-20 * panUnits, 0, -pole * 10 * panUnits};
        require(nearly_equal(camera.target(), initialTarget + expectedOffset, 0.000001f),
                "pole pan follows the deterministic +X right and signed Z up basis");
        require(camera.distance() == distance && finite(camera.view()) && camera.view() != Mat4{},
                "pole pan preserves distance and a valid view");
        camera.orbit(0, -pole * 20);
        const Vec3 expectedDirection{0, pole * std::cos(0.1f), std::sin(0.1f)};
        require(nearly_equal(normalize(camera.position() - camera.target()), expectedDirection, 0.000001f),
                "vertical orbit exits either pole with the existing drag sign");
        camera.orbit(0, pole * 1e8f);
        require(nearly_equal(normalize(camera.position() - camera.target()), Vec3(0, pole, 0), 0.000001f) &&
                    finite(camera.view()) && camera.view() != Mat4{},
                "orbit reaches the exact full pitch limit without a singular view");
        camera.orbit(80, 0);
        const auto marker = camera.target() + Vec3(0, 0, -pole);
        const auto markerInView = transform_point(camera.view(), marker);
        require(std::abs(markerInView.x) > 0.2f && finite(camera.view()),
                "horizontal orbit at a pole changes its screen basis without losing a valid view");
        camera.orbit(0, -pole * 20);
        require(finite(camera.view()) && camera.view() != Mat4{}, "orbit continues away from a rotated pole");
    }
}
void viewDirectionPreservesStateOnFailure() {
    using namespace aeg::math;
    aeg::camera::OrbitCamera camera;
    camera.fit({{-2, 3, -5}, {4, 9, 7}});
    camera.orbit(80, -50);
    camera.pan(20, 10, 600);
    camera.zoom(3);
    const auto target = camera.target();
    const auto position = camera.position();
    const auto view = camera.view();
    const auto projection = camera.projection(1.6f);
    const float distance = camera.distance();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const Vec3 invalid : {Vec3{}, Vec3{nan, 0, 1}, Vec3{0, infinity, 1}, Vec3{0, 1, -infinity}}) {
        require(!camera.setViewDirection(invalid), "reject zero and nonfinite view directions");
        require(camera.target() == target && camera.position() == position && camera.distance() == distance &&
                    camera.view() == view && camera.projection(1.6f) == projection,
                "failed view direction changes preserve all observable camera state");
    }
    require(camera.setViewDirection({0, -1, 0}), "valid view changes work after rejected inputs");
    camera.zoom(1);
    require(close(camera.distance(), distance * std::exp(-0.15f)) && camera.target() == target,
            "zoom after view snapping retains the previous fitted distance scale");
    const aeg::foundation::Bounds next{{1, 2, 3}, {5, 8, 11}};
    camera.fit(next);
    require(camera.target() == next.center() &&
                nearly_equal(camera.position(), next.center() + Vec3(0, -camera.distance(), 0), 0.000001f),
            "fit after view snapping changes bounds while preserving the selected pole");
    camera.zoom(1e8f);
    require(close(camera.distance(), next.radius() * 0.02f), "view snapping preserves fit-dependent zoom limits");
}
void closeViewPreservesLayerDepth() {
    for (const float scale : {0.01f, 1.0f, 100.0f}) {
        aeg::foundation::Bounds bounds;
        const float halfExtent = scale / std::sqrt(3.0f);
        bounds.min = aeg::math::Vec3(-halfExtent);
        bounds.max = aeg::math::Vec3(halfExtent);
        const float radius = bounds.radius();
        for (const float distanceInRadii : {0.5f, 1.0f, 1.5f}) {
            aeg::camera::OrbitCamera camera;
            camera.fit(bounds);
            camera.zoom(std::log(camera.distance() / (distanceInRadii * radius)) / 0.15f);
            const auto projection = camera.projection(1.6f);
            require(finite(projection), "close-view projection remains finite across model scales");
            // Two visible layers separated by 0.005% of the model radius must
            // retain enough float depth separation to avoid quantizing together.
            const auto bodyClip = projection * aeg::math::Vec4(0, 0, -camera.distance(), 1);
            const auto layerClip = projection * aeg::math::Vec4(0, 0, -camera.distance() + radius * 0.00005f, 1);
            const float bodyDepth = bodyClip.z / bodyClip.w;
            const float layerDepth = layerClip.z / layerClip.w;
            require(layerDepth > 0 && bodyDepth < 1, "close-view layers remain inside the depth clip range");
            const float depthUlp = bodyDepth - std::nextafter(bodyDepth, 0.0f);
            std::cout << "Close-view depth: radius " << radius << ", distance/radius " << distanceInRadii
                      << ", layer separation in float depth ULPs " << (bodyDepth - layerDepth) / depthUlp << '\n';
            require(bodyDepth - layerDepth >= 4 * depthUlp,
                    "close-view decorative layers retain at least four float depth ULPs");
        }
    }
}
void fullZoomPreservesNearbySurfaces() {
    for (const float scale : {0.01f, 1.0f, 100.0f}) {
        aeg::foundation::Bounds bounds;
        const float halfExtent = scale / std::sqrt(3.0f);
        bounds.min = aeg::math::Vec3(-halfExtent);
        bounds.max = aeg::math::Vec3(halfExtent);
        aeg::camera::OrbitCamera camera;
        camera.fit(bounds);
        camera.zoom(1e8f);
        const auto projection = camera.projection(1.6f);
        require(finite(projection), "full-zoom projection remains finite across model scales");
        const auto clip = projection * aeg::math::Vec4(0, 0, -camera.distance() * 0.25f, 1);
        require(clip.w > 0 && clip.z / clip.w > 0 && clip.z / clip.w < 1,
                "full zoom keeps nearby surfaces in front of the camera visible");
    }
}
void horizontalOrbitFollowsDrag() {
    aeg::foundation::Bounds bounds;
    bounds.min = {-1, -1, -1};
    bounds.max = {1, 1, 1};
    for (const float initialDrag : {-900.f, 0.f, 900.f}) {
        for (const float direction : {-1.f, 1.f}) {
            aeg::camera::OrbitCamera camera;
            camera.fit(bounds);
            camera.orbit(initialDrag, 0);
            const auto initialEye = camera.position();
            const auto marker = camera.target() + aeg::math::normalize(initialEye - camera.target()) * 0.5f;
            const auto screenX = [&] {
                const auto clip = camera.projection(1.6f) * camera.view() * aeg::math::Vec4(marker, 1);
                require(clip.w > 0, "front-surface marker remains visible during orbit");
                return clip.x / clip.w;
            };
            const float before = screenX();
            camera.orbit(direction * 80, 0);
            require(direction * (screenX() - before) > 0.01f, "visible model surface follows horizontal mouse drag");
            require(close(camera.position().y, initialEye.y), "horizontal drag preserves camera elevation");
        }
    }
}
void camera() {
    aeg::camera::OrbitCamera c;
    aeg::foundation::Bounds b;
    b.min = {-2, -1, -3};
    b.max = {2, 1, 3};
    c.fit(b);
    require(c.distance() > b.radius(), "fit keeps eye outside model");
    for (float aspect : {0.2f, 1.0f, 3.0f}) {
        const auto projection = c.projection(aspect, {true, false});
        require(finite(projection) && projection[1][1] < 0, "Vulkan finite Y-flipped projection");
        const auto vp = projection * c.view();
        for (int i = 0; i < 8; ++i) {
            aeg::math::Vec3 p{(i & 1) ? 2.f : -2.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 3.f : -3.f};
            const auto clip = vp * aeg::math::Vec4(p, 1);
            require(clip.w > 0 && std::abs(clip.x / clip.w) < 1 && std::abs(clip.y / clip.w) < 1 &&
                        clip.z / clip.w > 0 && clip.z / clip.w < 1,
                    "fit contains all corners across aspect ratios");
        }
    }
    c.orbit(1e8f, 1e8f);
    require(finite(c.view()), "orbit pitch clamps");
    c.zoom(1e8f);
    require(c.distance() > 0 && finite(c.view()), "zoom minimum finite");
    c.zoom(-1e8f);
    require(c.distance() < 1e8f && finite(c.projection(1)), "zoom maximum finite");
    c.pan(20, 10, 600);
    require(finite(c.view()), "pan remains finite");
    c.orbit(std::numeric_limits<float>::infinity(), 0);
    c.zoom(std::numeric_limits<float>::quiet_NaN());
    require(finite(c.view()), "ignore invalid input");
    require(finite(c.projection(0)), "zero viewport aspect safe");
}
void renderCameraContract() {
    using namespace aeg::math;
    const aeg::camera::RenderCamera defaults;
    require(defaults.view == identity() && defaults.projection == identity(), "render camera defaults are identity");
    require(!defaults.convention.y_down && !defaults.convention.reversed_z, "render camera defaults to RH zero-to-one");
    // Hand-derived noncommuting transforms: translate x by 2, then scale x by 3.
    aeg::camera::RenderCamera packet;
    packet.view = translation({2, 0, 0});
    packet.projection = scale({3, 1, 1});
    const auto clip = packet.view_projection() * Vec4(1, 0, 0, 1);
    require(clip == Vec4(9, 0, 0, 1), "render camera composes projection before view");

    aeg::camera::OrbitCamera camera;
    const auto standard = camera.render_camera(1.6f);
    require(standard.projection[1][1] > 0, "default orbit projection is Y-up");
    for (const bool yDown : {false, true}) {
        for (const bool reversed : {false, true}) {
            const ClipSpaceConvention convention{yDown, reversed};
            const auto result = camera.render_camera(1.6f, convention);
            require(result.convention == convention, "orbit render camera carries both convention flags");
            require(result.view == camera.view() && result.projection == camera.projection(1.6f, convention),
                    "orbit render camera carries its matching view and projection");
            require((result.projection[1][1] < 0) == yDown, "orbit projection applies explicit Y convention");
            const auto nearClip = result.projection * Vec4(0, 0, -camera.distance() * 0.5f, 1);
            const auto farClip = result.projection * Vec4(0, 0, -camera.distance(), 1);
            require(reversed ? nearClip.z / nearClip.w > farClip.z / farClip.w
                             : nearClip.z / nearClip.w < farClip.z / farClip.w,
                    "orbit projection applies explicit depth direction");
        }
    }
}
}
int main() {
    int failures = 0;
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"default view faces forward", defaultViewFacesForward},
        {"focus preserves orbit", focusPreservesOrbit},
        {"explicit view directions", explicitViewDirections},
        {"pole pan and orbit", polePanAndOrbit},
        {"view direction failure preserves state", viewDirectionPreservesStateOnFailure},
        {"orbit camera", camera},
        {"render camera matrix and convention contract", renderCameraContract},
        {"horizontal orbit follows drag", horizontalOrbitFollowsDrag},
        {"close view preserves layer depth", closeViewPreservesLayerDepth},
        {"full zoom preserves nearby surfaces", fullZoomPreservesNearbySurfaces}};
    for (const auto& [name, test] : tests) try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    return failures == 0 ? 0 : 1;
}
