#include <Bounds.hpp>
#include <OrbitCamera.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
bool finite(const glm::mat4& m) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(m[c][r])) return false;
    return true;
}
void closeViewPreservesLayerDepth() {
    for (const float scale : {0.01f, 1.0f, 100.0f}) {
        aeg::foundation::Bounds bounds;
        const float halfExtent = scale / std::sqrt(3.0f);
        bounds.min = glm::vec3(-halfExtent);
        bounds.max = glm::vec3(halfExtent);
        const float radius = bounds.radius();
        for (const float distanceInRadii : {0.5f, 1.0f, 1.5f}) {
            aeg::camera::OrbitCamera camera;
            camera.fit(bounds);
            camera.zoom(std::log(camera.distance() / (distanceInRadii * radius)) / 0.15f);
            const auto projection = camera.projection(1.6f);
            require(finite(projection), "close-view projection remains finite across model scales");
            // Two visible layers separated by 0.005% of the model radius must
            // retain enough float depth separation to avoid quantizing together.
            const auto bodyClip = projection * glm::vec4(0, 0, -camera.distance(), 1);
            const auto layerClip = projection * glm::vec4(0, 0, -camera.distance() + radius * 0.00005f, 1);
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
        bounds.min = glm::vec3(-halfExtent);
        bounds.max = glm::vec3(halfExtent);
        aeg::camera::OrbitCamera camera;
        camera.fit(bounds);
        camera.zoom(1e8f);
        const auto projection = camera.projection(1.6f);
        require(finite(projection), "full-zoom projection remains finite across model scales");
        const auto clip = projection * glm::vec4(0, 0, -camera.distance() * 0.25f, 1);
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
            const auto marker = camera.target() + glm::normalize(initialEye - camera.target()) * 0.5f;
            const auto screenX = [&] {
                const auto clip = camera.projection(1.6f) * camera.view() * glm::vec4(marker, 1);
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
        const auto projection = c.projection(aspect);
        require(finite(projection) && projection[1][1] < 0, "Vulkan finite Y-flipped projection");
        const auto vp = projection * c.view();
        for (int i = 0; i < 8; ++i) {
            glm::vec3 p{(i & 1) ? 2.f : -2.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 3.f : -3.f};
            const auto clip = vp * glm::vec4(p, 1);
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
}
int main() {
    int failures = 0;
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"orbit camera", camera},
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
