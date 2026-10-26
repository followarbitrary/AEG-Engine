#include "shadow/ShadowFitting.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace aeg;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void close(float actual, float expected, float tolerance, const std::string& message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
render::GpuLight directional(math::Vec3 direction = {0.25f, -0.5f, -1}) {
    render::GpuLight light;
    light.directionType = {math::normalize(direction), static_cast<float>(render::LightType::Directional)};
    light.radiance = {1, 1, 1, 1};
    return light;
}
render::RenderSettings camera(bool perspective, math::ClipSpaceConvention convention = {true, false}, float scale = 1,
                              math::Vec3 eye = {}, float yaw = 0) {
    render::RenderSettings settings;
    const math::Vec3 forward{-std::sin(yaw), 0, -std::cos(yaw)};
    const auto view = math::look_at(eye, eye + forward * scale, {0, 1, 0});
    const auto projection = perspective ? math::perspective(1, 1.3f, 0.25f * scale, 80 * scale, convention)
                                        : math::orthographic(-3 * scale, 5 * scale, -2 * scale, 4 * scale,
                                                             0.25f * scale, 80 * scale, convention);
    settings.set_camera({view, projection, convention});
    return settings;
}
math::Vec3 sliceCorner(const render::RenderSettings& settings, bool perspective, float depth, unsigned corner,
                       float scale = 1) {
    // Independent camera geometry: no inverse VP or production fitting helper.
    const auto forward = -math::normalize(settings.cameraTowardViewer);
    const auto right = math::normalize(math::cross(forward, {0, 1, 0}));
    const auto up = math::cross(right, forward);
    const float x = perspective ? ((corner & 1) ? 1.0f : -1.0f) * std::tan(0.5f) * 1.3f * depth
                                : ((corner & 1) ? 5.0f : -3.0f) * scale;
    const float y =
        perspective ? ((corner & 2) ? 1.0f : -1.0f) * std::tan(0.5f) * depth : ((corner & 2) ? 4.0f : -2.0f) * scale;
    return settings.cameraPosition + forward * depth + right * x + up * y;
}
void insideXY(const math::Mat4& matrix, math::Vec3 point, const std::string& message) {
    const auto clip = matrix * math::Vec4(point, 1);
    require(math::is_finite(clip) && clip.w > 0 && std::abs(clip.x / clip.w) <= 1.001f &&
                std::abs(clip.y / clip.w) <= 1.001f,
            message);
}
void casterDepth(const render::ShadowFit& fit, const render::RenderSettings& settings, math::Vec3 minimum,
                 math::Vec3 maximum) {
    for (unsigned corner = 0; corner < 8; ++corner) {
        const math::Vec3 local{(corner & 1) ? maximum.x : minimum.x, (corner & 2) ? maximum.y : minimum.y,
                               (corner & 4) ? maximum.z : minimum.z};
        const auto world = math::transform_point(settings.model, local);
        for (unsigned cascade = 0; cascade < fit.count; ++cascade) {
            const auto clip = fit.matrices[cascade] * math::Vec4(world, 1);
            require(math::is_finite(clip) && clip.w > 0 && clip.z / clip.w >= 0 && clip.z / clip.w <= 1,
                    "Cascade depth clipped an off-camera transformed caster bounds corner");
        }
    }
}
void conventionsAndCoverage() {
    // Removing camera reconstruction, overlap expansion, or all-caster Z fitting
    // must fail different geometric assertions in this fixture.
    render::ShadowSettings shadows;
    shadows.enabled = true;
    shadows.resolution = 512;
    shadows.maxDistance = 37;
    shadows.pcfRadius = 2;
    const math::Vec3 minimum{-120, -5, -30}, maximum{5, 8, 4};
    for (bool perspective : {false, true})
        for (bool yDown : {false, true})
            for (bool reversed : {false, true}) {
                auto settings = camera(perspective, {yDown, reversed}, 1, {3, -2, 6}, 0.3f);
                settings.model = math::translation({1.5f, -2, -4}) * math::rotation({0, 1, 0}, 0.7f) *
                                 math::scale({1.3f, 0.5f, 2.1f});
                render::ShadowFit fit;
                require(render::tryFitShadow(settings, shadows, minimum, maximum, directional(), fit),
                        "Valid camera did not produce directional cascades");
                require(fit.count == 4, "Directional CSM did not activate four cascades");
                close(fit.splits[3], 37, 0.0001f, "Shadow maximum distance did not clamp camera far");
                float previous = 0.25f;
                for (unsigned cascade = 0; cascade < fit.count; ++cascade) {
                    require(fit.splits[cascade] > previous, "Cascade splits are not strictly increasing");
                    const float first = cascade == 0 ? 0.25f : fit.blendStarts[cascade - 1];
                    for (float depth : {first, fit.splits[cascade]})
                        for (unsigned corner = 0; corner < 4; ++corner)
                            insideXY(fit.matrices[cascade], sliceCorner(settings, perspective, depth, corner),
                                     "Cascade XY clipped its receiver slice or overlap");
                    const float fraction =
                        cascade + 1 < fit.count ? shadows.blendFraction : shadows.distanceFadeFraction;
                    close(fit.blendStarts[cascade], fit.splits[cascade] - fraction * (fit.splits[cascade] - previous),
                          0.001f, "Cascade overlap or final fade does not follow its own interval");
                    previous = fit.splits[cascade];
                }
                casterDepth(fit, settings, minimum, maximum);
            }
}
void splitPolicy() {
    auto settings = camera(true);
    render::ShadowSettings shadows;
    shadows.enabled = true;
    shadows.cascadeCount = 4;
    shadows.maxDistance = 64;
    shadows.blendFraction = 0;
    shadows.distanceFadeFraction = 0;
    // n=.25, F=64: logarithmic endpoints are exactly 1,4,16,64;
    // linear endpoints are 16.1875,32.125,48.0625,64.
    constexpr std::array linear{16.1875f, 32.125f, 48.0625f, 64.0f};
    constexpr std::array logarithmic{1.0f, 4.0f, 16.0f, 64.0f};
    for (float lambda : {0.0f, 0.5f, 1.0f}) {
        shadows.splitLambda = lambda;
        render::ShadowFit fit;
        require(render::tryFitShadow(settings, shadows, {-1, -1, -5}, {1, 1, -2}, directional(), fit),
                "Practical split policy did not fit");
        for (unsigned i = 0; i < 4; ++i) {
            close(fit.splits[i], linear[i] * (1 - lambda) + logarithmic[i] * lambda, 0.002f,
                  "Practical split does not match hand-derived policy");
            close(fit.blendStarts[i], fit.splits[i], 0.0001f, "Zero overlap/fade still expands intervals");
        }
    }
    shadows.maxDistance = 0;
    for (unsigned count : {2u, 3u, 4u}) {
        shadows.cascadeCount = count;
        render::ShadowFit fit;
        require(render::tryFitShadow(settings, shadows, {-1, -1, -5}, {1, 1, -2}, directional(), fit),
                "Variable active cascade count did not fit");
        require(fit.count == count, "Requested directional cascade count was ignored");
        close(fit.splits[count - 1], 80, 0.02f, "Auto shadow distance did not reach camera far");
        for (unsigned i = count; i < 4; ++i)
            require(fit.matrices[i] == math::identity() && fit.splits[i] == 0 && fit.blendStarts[i] == 0,
                    "Inactive cascade contains stale matrix or metadata");
    }
}
void boundedDistanceWithDistantCameraFar() {
    // A normal-depth .1..1e7 perspective projection rounds its Z coefficient
    // to -1, so the inverse far endpoint has w=0. A finite shadow distance
    // still defines ordinary, finite receiver slices on the interior rays.
    render::ShadowSettings shadows;
    shadows.enabled = true;
    shadows.maxDistance = 100;
    for (bool yDown : {false, true})
        for (bool reversed : {false, true}) {
            const math::ClipSpaceConvention convention{yDown, reversed};
            render::RenderSettings settings;
            settings.set_camera({math::identity(), math::perspective(1, 1, 0.1f, 1e7f, convention), convention});
            math::Mat4 inverse;
            require(math::try_inverse(settings.viewProjection, inverse, 0),
                    "Distant camera far fixture is not an invertible public projection");
            render::ShadowFit fit;
            require(render::tryFitShadow(settings, shadows, {-5, -5, -100}, {5, 5, -1}, directional(), fit),
                    "Finite shadow distance was rejected because camera far unprojects to infinity");
            require(fit.count == 4, "Distant-camera fit lost requested cascade count");
            close(fit.splits[3], 100, 0.0001f, "Finite shadow distance did not bound distant camera");
            for (unsigned corner = 0; corner < 4; ++corner) {
                math::Vec3 near, interior;
                const float x = (corner & 1) ? 1.0f : -1.0f, y = (corner & 2) ? 1.0f : -1.0f;
                require(math::try_transform_point(inverse, {x, y, reversed ? 1.0f : 0.0f}, near) &&
                            math::try_transform_point(inverse, {x, y, 0.5f}, interior),
                        "Distant camera does not have finite near and interior rays");
                for (unsigned cascade = 0; cascade < fit.count; ++cascade) {
                    const float first = cascade == 0 ? -near.z : fit.blendStarts[cascade - 1];
                    for (float depth : {first, fit.splits[cascade]}) {
                        const auto point = near + (interior - near) * ((depth + near.z) / (near.z - interior.z));
                        insideXY(fit.matrices[cascade], point,
                                 "Bounded distant-camera cascade clipped interior-ray slice/overlap");
                    }
                }
            }
            casterDepth(fit, settings, {-5, -5, -100}, {5, 5, -1});
            if (!reversed) {
                auto autoDistance = shadows;
                autoDistance.maxDistance = 0;
                auto unchanged = fit;
                require(!render::tryFitShadow(settings, autoDistance, {-5, -5, -100}, {5, 5, -1}, directional(),
                                              unchanged) &&
                            unchanged.matrices == fit.matrices && unchanged.splits == fit.splits &&
                            unchanged.blendStarts == fit.blendStarts && unchanged.count == fit.count,
                        "Unbounded infinite far did not fail transactionally");
            }
            settings.set_camera({math::identity(), math::perspective(1, 1, 0.1f, 20, convention), convention});
            require(render::tryFitShadow(settings, shadows, {-5, -5, -100}, {5, 5, -1}, directional(), fit),
                    "Finite far shorter than shadow distance no longer fits");
            close(fit.splits[3], 20, 0.002f, "Positive shadow cap extended beyond finite camera far");
        }
}
void offCameraCaster() {
    render::RenderSettings settings;
    settings.set_camera({math::identity(), math::orthographic(-2, 2, -2, 2, 0.1f, 10, {true, false}), {true, false}});
    render::ShadowSettings shadows;
    shadows.enabled = true;
    const auto light = directional({10, 0, 1});
    render::ShadowFit fit;
    require(render::tryFitShadow(settings, shadows, {-101, -1, -14}, {1, 1, -4}, light, fit),
            "Far off-camera caster invalidated receiver cascades");
    const math::Vec3 receiver{0, 0, -4}, caster{-100, 0, -14};
    for (unsigned i = 0; i < fit.count; ++i) {
        const auto receiverClip = fit.matrices[i] * math::Vec4(receiver, 1);
        const auto casterClip = fit.matrices[i] * math::Vec4(caster, 1);
        close(receiverClip.x, casterClip.x, 0.0001f, "Adversarial caster no longer lies on receiver light ray");
        close(receiverClip.y, casterClip.y, 0.0001f, "Adversarial caster light-ray Y differs");
        require(casterClip.z >= 0 && casterClip.z < receiverClip.z && receiverClip.z <= 1,
                "Off-camera caster was clipped by receiver-frustum light depth");
    }
    // The distant caster must not destroy near-cascade texel density.
    const auto& nearMatrix = fit.matrices[0];
    const float xyScale = math::length(math::Vec3{nearMatrix.at(0, 0), nearMatrix.at(0, 1), nearMatrix.at(0, 2)});
    require(2 / xyScale < 10, "Whole-model XY expansion unexpectedly consumed the near cascade");
    casterDepth(fit, settings, {-101, -1, -14}, {1, 1, -4});
}
void stableProjection() {
    auto settings = camera(true);
    render::ShadowSettings shadows;
    shadows.enabled = true;
    shadows.resolution = 1024;
    shadows.maxDistance = 64;
    const auto light = directional({0, 0, -1});
    render::ShadowFit original;
    require(render::tryFitShadow(settings, shadows, {-100, -100, -100}, {100, 100, 100}, light, original),
            "Stable projection reference did not fit");
    const float texel = 2 / (original.matrices[0].at(0, 0) * float(shadows.resolution));
    for (float movement : {0.2f, 0.8f}) {
        settings = camera(true, {true, false}, 1, {movement * texel, 0, 0});
        render::ShadowFit moved;
        require(render::tryFitShadow(settings, shadows, {-100, -100, -100}, {100, 100, 100}, light, moved),
                "Translated camera did not fit");
        close(moved.matrices[0].at(0, 0), original.matrices[0].at(0, 0), 0,
              "Subtexel camera motion changed the stable projection size");
        const float expectedOffset = movement < 0.5f ? 0 : -2.0f / float(shadows.resolution);
        close(moved.matrices[0].at(0, 3) - original.matrices[0].at(0, 3), expectedOffset, 1e-7f,
              "Projection did not remain fixed then move by one texel across snapping boundary");
    }
    settings = camera(true, {true, false}, 1, {}, 0.55f);
    render::ShadowFit rotated;
    require(render::tryFitShadow(settings, shadows, {-100, -100, -100}, {100, 100, 100}, light, rotated),
            "Rotated camera did not fit");
    for (unsigned i = 0; i < 4; ++i)
        close(rotated.matrices[i].at(0, 0), original.matrices[i].at(0, 0), 0,
              "Camera rotation changed stable cascade texel size");
}
bool same(const render::ShadowFit& a, const render::ShadowFit& b) {
    return a.matrices == b.matrices && a.splits == b.splits && a.blendStarts == b.blendStarts && a.count == b.count;
}
void failureIsTransactional() {
    const auto valid = camera(true);
    render::ShadowSettings shadows;
    shadows.enabled = true;
    render::ShadowFit sentinel;
    sentinel.matrices[0] = math::translation({12, 34, 56});
    sentinel.splits = {1, 2, 3, 4};
    sentinel.blendStarts = {5, 6, 7, 8};
    sentinel.count = 3;
    const auto fails = [&](const render::RenderSettings& settings, const render::ShadowSettings& policy,
                           const render::GpuLight& light, math::Vec3 minimum = {-1, -1, -5},
                           math::Vec3 maximum = {1, 1, -2}) {
        auto output = sentinel;
        require(!render::tryFitShadow(settings, policy, minimum, maximum, light, output),
                "Invalid fitting input was accepted");
        require(same(output, sentinel), "Failed fit published partial cascade output");
    };
    auto invalid = valid;
    invalid.viewProjection = {};
    fails(invalid, shadows, directional());
    invalid = valid;
    invalid.viewProjection.at(0, 0) = std::numeric_limits<float>::quiet_NaN();
    fails(invalid, shadows, directional());
    invalid = valid;
    invalid.cameraPosition.x = std::numeric_limits<float>::infinity();
    fails(invalid, shadows, directional());
    invalid = valid;
    invalid.cameraTowardViewer = {};
    fails(invalid, shadows, directional());
    invalid = valid;
    invalid.model.at(3, 0) = 0.25f;
    fails(invalid, shadows, directional());
    auto policy = shadows;
    policy.maxDistance = 0.1f;
    fails(valid, policy, directional());
    policy = shadows;
    policy.splitLambda = std::numeric_limits<float>::quiet_NaN();
    fails(valid, policy, directional());
    policy = shadows;
    policy.cascadeCount = 0;
    fails(valid, policy, directional());
    auto light = directional();
    light.directionType = {0, 0, 0, 0};
    fails(valid, shadows, light);
    light = directional();
    light.directionType.w = static_cast<float>(render::LightType::Point);
    fails(valid, shadows, light);
    fails(valid, shadows, directional(), {1, 1, 1}, {-1, -1, -1});
}
void scaleAndLegacy() {
    render::ShadowSettings shadows;
    shadows.enabled = true;
    shadows.resolution = 512;
    for (float scale : {1e-12f, 1.0f, 1e12f})
        for (bool perspective : {false, true}) {
            const auto settings = camera(perspective, {false, true}, scale);
            render::ShadowFit fit;
            require(render::tryFitShadow(settings, shadows, {-5 * scale, -scale, -20 * scale},
                                         {5 * scale, scale, -2 * scale}, directional({0, -1, 0}), fit),
                    "Finite tiny/large camera or vertical light did not fit");
            for (unsigned i = 0; i < fit.count; ++i)
                for (unsigned corner = 0; corner < 4; ++corner)
                    insideXY(fit.matrices[i], sliceCorner(settings, perspective, fit.splits[i], corner, scale),
                             "Scaled or vertical-light projection clipped receiver XY");
            casterDepth(fit, settings, {-5 * scale, -scale, -20 * scale}, {5 * scale, scale, -2 * scale});
        }
    render::RenderSettings noCamera;
    noCamera.viewProjection = {};
    shadows.cascadeCount = 1;
    shadows.maxDistance = 0.01f;
    for (unsigned radius : {0u, 1u, 2u}) {
        shadows.pcfRadius = radius;
        render::ShadowFit fit;
        require(render::tryFitShadow(noCamera, shadows, {-1, -1, -5}, {1, 1, -2}, directional({0, 0, -1}), fit) &&
                    fit.count == 1,
                "Legacy directional fit incorrectly requires a camera or distance cutoff");
        const auto edge = fit.matrices[0] * math::Vec4(1, 1, -3, 1);
        require(std::abs(edge.x) <= 1 - 2 * (float(radius) + 0.5f) / float(shadows.resolution),
                "Legacy fit did not reserve enough texels for the selected PCF radius");
        for (unsigned corner = 0; corner < 8; ++corner)
            insideXY(fit.matrices[0],
                     {(corner & 1) ? 1.0f : -1.0f, (corner & 2) ? 1.0f : -1.0f, (corner & 4) ? -2.0f : -5.0f},
                     "Legacy model fit clipped a bounds corner");
    }
    auto spot = directional({0, 0, -1});
    spot.directionType.w = static_cast<float>(render::LightType::Spot);
    spot.positionRange = {0, 0, 0, 30};
    spot.cone = {std::cos(0.25f), std::cos(0.5f), 0, 0};
    shadows.cascadeCount = 4;
    render::ShadowFit spotFit;
    require(render::tryFitShadow(noCamera, shadows, {-1, -1, -5}, {1, 1, -2}, spot, spotFit) && spotFit.count == 1,
            "Spot fit incorrectly follows directional cascade/camera settings");
    for (unsigned i = 1; i < 4; ++i)
        require(spotFit.matrices[i] == math::identity(), "Spot fit contains inactive stale matrices");
}
}
int main() {
    try {
        conventionsAndCoverage();
        splitPolicy();
        boundedDistanceWithDistantCameraFar();
        offCameraCaster();
        stableProjection();
        failureIsTransactional();
        scaleAndLegacy();
        std::cout << "PASS cascade fitting: slices, caster depth, splits, overlap, snapping, scales and failures\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL cascade fitting: " << error.what() << '\n';
        return 1;
    }
}
