#include <Bounds.hpp>
#include <OrbitCamera.hpp>
#include <RenderCamera.hpp>
#include <math.hpp>
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
