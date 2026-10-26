#include "ShadowFitting.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace aeg::render {
namespace {
using Corners = std::array<math::DVec3, 8>;
constexpr double floatError = 8.0 * std::numeric_limits<float>::epsilon();

bool transformedCorners(math::Vec3 minimum, math::Vec3 maximum, const math::Mat4& transform, Corners& corners) {
    if (!math::is_finite(minimum) || !math::is_finite(maximum) || minimum.x > maximum.x || minimum.y > maximum.y ||
        minimum.z > maximum.z || !math::is_finite(transform) || transform.at(3, 0) != 0 || transform.at(3, 1) != 0 ||
        transform.at(3, 2) != 0 || transform.at(3, 3) != 1)
        return false;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const math::DVec3 local{(i & 1) ? maximum.x : minimum.x, (i & 2) ? maximum.y : minimum.y,
                                (i & 4) ? maximum.z : minimum.z};
        for (std::size_t row = 0; row < 3; ++row) {
            double value = transform.at(row, 3);
            for (std::size_t column = 0; column < 3; ++column)
                value += static_cast<double>(transform.at(row, column)) * local[column];
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return false;
            corners[i][row] = value;
        }
    }
    return true;
}

struct LightBasis {
    math::DVec3 forward, right, up;
};
bool lightBasis(math::Vec3 direction, LightBasis& basis) {
    if (!math::try_normalize(math::DVec3(direction), basis.forward)) return false;
    // Choosing an axis away from the direction avoids the vertical-light singularity.
    const math::DVec3 reference = std::abs(basis.forward.y) < 0.9 ? math::DVec3{0, 1, 0} : math::DVec3{1, 0, 0};
    if (!math::try_normalize(math::cross(basis.forward, reference), basis.right)) return false;
    basis.up = math::cross(basis.right, basis.forward);
    return math::is_finite(basis.up);
}
bool usableMatrix(const math::Mat4& matrix) {
    math::Mat4 inverse;
    return math::is_finite(matrix) && math::try_inverse(matrix, inverse, 0);
}
math::DVec3 lightPoint(const LightBasis& basis, math::DVec3 point) {
    return {math::dot(basis.right, point), math::dot(basis.up, point), math::dot(basis.forward, point)};
}
bool orthographicMatrix(const LightBasis& basis, double centerX, double centerY, double width, double height,
                        double minimumDepth, double maximumDepth, double nonzeroScale, math::Mat4& output) {
    const double depthPadding =
        std::max(nonzeroScale * 0.02, std::max(std::abs(minimumDepth), std::abs(maximumDepth)) * floatError);
    const double firstDepth = minimumDepth - depthPadding;
    const double depthRange = maximumDepth - minimumDepth + 2 * depthPadding;
    if (!(width > 0) || !(height > 0) || !(depthRange > 0) || !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(depthRange) || !std::isfinite(firstDepth) || !std::isfinite(centerX) || !std::isfinite(centerY))
        return false;
    // P*V for a right-handed light view, y-down, non-reversed [0,1] depth.
    // Forming rows in double avoids constructing a far-away intermediate float eye.
    auto matrix = math::identity();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        matrix.at(0, axis) = static_cast<float>(2 * basis.right[axis] / width);
        matrix.at(1, axis) = static_cast<float>(-2 * basis.up[axis] / height);
        matrix.at(2, axis) = static_cast<float>(basis.forward[axis] / depthRange);
    }
    matrix.at(0, 3) = static_cast<float>(-2 * centerX / width);
    matrix.at(1, 3) = static_cast<float>(2 * centerY / height);
    matrix.at(2, 3) = static_cast<float>(-firstDepth / depthRange);
    if (!usableMatrix(matrix)) return false;
    output = matrix;
    return true;
}
bool fits(const math::Mat4& matrix, const Corners& corners, bool requireXY) {
    for (const auto& corner : corners) {
        const auto clip = matrix * math::Vec4(math::Vec3(corner), 1);
        if (!math::is_finite(clip) || (requireXY && (std::abs(clip.x) > 1.0001f || std::abs(clip.y) > 1.0001f)) ||
            clip.z < 0 || clip.z > 1)
            return false;
    }
    return true;
}
bool directionalProjection(const Corners& corners, const LightBasis& basis, std::uint32_t resolution,
                           std::uint32_t pcfRadius, math::Mat4& output) {
    math::DVec3 minimum(std::numeric_limits<double>::max()), maximum(std::numeric_limits<double>::lowest());
    for (const auto& corner : corners) {
        const auto projected = lightPoint(basis, corner);
        minimum = math::min(minimum, projected);
        maximum = math::max(maximum, projected);
    }
    const auto span = maximum - minimum;
    const double scale = std::max({span.x, span.y, span.z});
    const double nonzeroScale = scale > 0 ? scale : 1.0;
    const double widthError = std::max(std::abs(minimum.x), std::abs(maximum.x)) * floatError;
    const double heightError = std::max(std::abs(minimum.y), std::abs(maximum.y)) * floatError;
    // Each side reserves the filter radius plus one texel for center snapping
    // and the nearest-sample center offset. Radius one preserves the old fit.
    const double mapSize = resolution, padding = pcfRadius + 1;
    const double width = (std::max(span.x, nonzeroScale * 1e-4) + 2 * widthError) * mapSize / (mapSize - 2 * padding);
    const double height = (std::max(span.y, nonzeroScale * 1e-4) + 2 * heightError) * mapSize / (mapSize - 2 * padding);
    const double texelX = width / mapSize, texelY = height / mapSize;
    if (!(texelX > 0) || !(texelY > 0) || !std::isfinite(texelX) || !std::isfinite(texelY)) return false;
    const double centerX = std::floor(((minimum.x + maximum.x) * 0.5) / texelX + 0.5) * texelX;
    const double centerY = std::floor(((minimum.y + maximum.y) * 0.5) / texelY + 0.5) * texelY;
    math::Mat4 matrix;
    if (!orthographicMatrix(basis, centerX, centerY, width, height, minimum.z, maximum.z, nonzeroScale, matrix) ||
        !fits(matrix, corners, true))
        return false;
    output = matrix;
    return true;
}
bool spotProjection(const Corners& corners, const LightBasis& basis, const GpuLight& light, math::Mat4& output) {
    const double farDistance = light.positionRange.w;
    const double fov = 2 * std::acos(std::clamp(static_cast<double>(light.cone.y), -1.0, 1.0));
    if (!(farDistance > 0) || !std::isfinite(farDistance) || !(fov > 0) || !(fov < math::kPi)) return false;
    const math::DVec3 position{math::Vec3(light.positionRange)};
    double closestDepth = std::numeric_limits<double>::max();
    for (const auto& corner : corners)
        closestDepth = std::min(closestDepth, math::dot(basis.forward, corner - position));
    // A fit entirely in front of the light lets the near plane follow its bounds.
    const double nearDistance = closestDepth > 0 ? std::min(farDistance * 0.1, closestDepth * 0.5) : farDistance * 1e-4;
    if (!(nearDistance > 0) || !(nearDistance < farDistance)) return false;
    const double focal = 1 / std::tan(fov * 0.5);
    const double depthScale = farDistance / (farDistance - nearDistance);
    const double depthOffset = farDistance * nearDistance / (farDistance - nearDistance);
    math::Mat4 matrix;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        matrix.at(0, axis) = static_cast<float>(focal * basis.right[axis]);
        matrix.at(1, axis) = static_cast<float>(-focal * basis.up[axis]);
        matrix.at(2, axis) = static_cast<float>(depthScale * basis.forward[axis]);
        matrix.at(3, axis) = static_cast<float>(basis.forward[axis]);
    }
    matrix.at(0, 3) = static_cast<float>(-focal * math::dot(basis.right, position));
    matrix.at(1, 3) = static_cast<float>(focal * math::dot(basis.up, position));
    matrix.at(2, 3) = static_cast<float>(-depthScale * math::dot(basis.forward, position) - depthOffset);
    matrix.at(3, 3) = static_cast<float>(-math::dot(basis.forward, position));
    if (!usableMatrix(matrix)) return false;
    output = matrix;
    return true;
}

struct CameraFrustum {
    Corners corners; // Four near corners followed by four finite interior-ray points.
    std::array<double, 4> nearDepths{}, rayDepths{};
    double nearDistance{}, farDistance{};
};
bool cameraFrustum(const RenderSettings& settings, bool boundedDistance, CameraFrustum& frustum) {
    math::DVec3 towardViewer;
    math::Mat4 inverse;
    if (!math::is_finite(settings.cameraPosition) ||
        !math::try_normalize(math::DVec3(settings.cameraTowardViewer), towardViewer) ||
        !math::try_inverse(settings.viewProjection, inverse, 0))
        return false;
    const math::DVec3 eye(settings.cameraPosition), forward = -towardViewer;
    frustum.nearDistance = std::numeric_limits<double>::max();
    frustum.farDistance = 0;
    const auto unproject = [&](std::size_t corner, double clipDepth, math::DVec3& point, double& w) {
        const std::array<double, 4> ndc{(corner & 1) ? 1.0 : -1.0, (corner & 2) ? 1.0 : -1.0, clipDepth, 1.0};
        std::array<double, 4> homogeneous{};
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
                homogeneous[row] += static_cast<double>(inverse.at(row, column)) * ndc[column];
        w = homogeneous[3];
        if (!std::isfinite(w) || w == 0) return false;
        for (std::size_t axis = 0; axis < 3; ++axis) point[axis] = homogeneous[axis] / w;
        return math::is_finite(point);
    };
    for (std::size_t i = 0; i < 8; ++i) {
        const bool interior = (i & 4) != 0;
        const double clipDepth = interior ? 0.5 : (settings.clipSpace.reversed_z ? 1.0 : 0.0);
        double w{};
        if (!unproject(i, clipDepth, frustum.corners[i], w)) return false;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (std::abs(frustum.corners[i][axis]) > std::numeric_limits<float>::max()) return false;
        }
        const double depth = math::dot(forward, frustum.corners[i] - eye);
        if (!(depth > 0) || !std::isfinite(depth)) return false;
        if (interior)
            frustum.rayDepths[i & 3] = depth;
        else {
            frustum.nearDepths[i] = depth;
            frustum.nearDistance = std::min(frustum.nearDistance, depth);
        }
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if (!(frustum.rayDepths[i] > frustum.nearDepths[i])) return false;
        math::DVec3 farCorner;
        double w{};
        if (!unproject(i, settings.clipSpace.reversed_z ? 0.0 : 1.0, farCorner, w)) {
            // A finite perspective far distance can round to an infinite far
            // endpoint in the public float projection. A positive shadow cap
            // still defines finite slices on the near/interior rays above.
            if (!boundedDistance || w != 0) return false;
            frustum.farDistance = std::numeric_limits<double>::infinity();
        } else {
            const double depth = math::dot(forward, farCorner - eye);
            if (!(depth > frustum.rayDepths[i]) || !std::isfinite(depth)) return false;
            frustum.farDistance = std::max(frustum.farDistance, depth);
        }
    }
    return frustum.farDistance > frustum.nearDistance;
}
bool sliceCorners(const CameraFrustum& frustum, double first, double last, Corners& output) {
    for (std::size_t i = 0; i < 8; ++i) {
        const std::size_t ray = i & 3;
        const double depth = (i & 4) ? last : first;
        // Interpolate in world-space view depth along each ray, not in nonlinear
        // NDC depth. Per-ray depths also avoid relying on exact float coplanarity.
        const double t = (depth - frustum.nearDepths[ray]) / (frustum.rayDepths[ray] - frustum.nearDepths[ray]);
        output[i] = frustum.corners[ray] + (frustum.corners[ray + 4] - frustum.corners[ray]) * t;
        if (!math::is_finite(output[i])) return false;
    }
    return true;
}
bool cascadeProjection(const Corners& slice, const Corners& casters, const LightBasis& basis,
                       const ShadowSettings& settings, math::Mat4& output) {
    math::DVec3 center{};
    for (const auto& corner : slice) center += corner / 8.0;
    double radius = 0;
    for (const auto& corner : slice) radius = std::max(radius, math::length(corner - center));
    if (!(radius > 0) || !std::isfinite(radius)) return false;
    const auto projectedCenter = lightPoint(basis, center);
    const double coordinateError =
        std::max({radius, std::abs(projectedCenter.x), std::abs(projectedCenter.y)}) * floatError;
    // A relative power-of-two quantum stabilizes radius against float camera
    // reconstruction noise, including very small and very large scene units.
    const double quantum = std::ldexp(1.0, std::ilogb(radius) - 10);
    radius = std::ceil((radius + coordinateError) / quantum) * quantum;
    const double mapSize = settings.resolution, padding = settings.pcfRadius + 1;
    const double width = 2 * radius * mapSize / (mapSize - 2 * padding);
    const double texel = width / mapSize;
    if (!(texel > 0) || !std::isfinite(texel)) return false;
    const double centerX = std::floor(projectedCenter.x / texel + 0.5) * texel;
    const double centerY = std::floor(projectedCenter.y / texel + 0.5) * texel;
    double minimumDepth = std::numeric_limits<double>::max(), maximumDepth = std::numeric_limits<double>::lowest();
    const auto includeDepth = [&](const Corners& corners) {
        for (const auto& corner : corners) {
            const double depth = math::dot(basis.forward, corner);
            minimumDepth = std::min(minimumDepth, depth);
            maximumDepth = std::max(maximumDepth, depth);
        }
    };
    // Receiver XY is deliberately separate from caster Z. An occluder beyond
    // the camera far/side planes can still lie on a visible receiver's light ray.
    includeDepth(casters);
    includeDepth(slice);
    math::Mat4 matrix;
    if (!orthographicMatrix(basis, centerX, centerY, width, width, minimumDepth, maximumDepth,
                            std::max(2 * radius, maximumDepth - minimumDepth), matrix) ||
        !fits(matrix, slice, true) || !fits(matrix, casters, false))
        return false;
    output = matrix;
    return true;
}
bool validPolicy(const ShadowSettings& settings) {
    return settings.cascadeCount >= 1 && settings.cascadeCount <= kMaxShadowCascades && settings.resolution >= 16 &&
           settings.resolution <= 4096 && std::has_single_bit(settings.resolution) && settings.pcfRadius <= 2 &&
           std::isfinite(settings.splitLambda) && settings.splitLambda >= 0 && settings.splitLambda <= 1 &&
           std::isfinite(settings.maxDistance) && settings.maxDistance >= 0 && std::isfinite(settings.blendFraction) &&
           settings.blendFraction >= 0 && settings.blendFraction <= 0.3f &&
           std::isfinite(settings.distanceFadeFraction) && settings.distanceFadeFraction >= 0 &&
           settings.distanceFadeFraction <= 0.5f;
}
}

bool tryFitShadow(const RenderSettings& settings, const ShadowSettings& shadows, math::Vec3 minimum, math::Vec3 maximum,
                  const GpuLight& light, ShadowFit& out) {
    const bool directional = light.directionType.w == static_cast<float>(LightType::Directional);
    const bool spot = light.directionType.w == static_cast<float>(LightType::Spot);
    if (!validPolicy(shadows) || (!directional && !spot) || !(light.radiance.w >= 0.5f)) return false;
    Corners casters;
    LightBasis basis;
    if (!transformedCorners(minimum, maximum, settings.model, casters) ||
        !lightBasis(math::Vec3(light.directionType), basis))
        return false;
    ShadowFit candidate;
    if (spot || shadows.cascadeCount == 1) {
        const bool fitted =
            spot ? spotProjection(casters, basis, light, candidate.matrices[0])
                 : directionalProjection(casters, basis, shadows.resolution, shadows.pcfRadius, candidate.matrices[0]);
        if (!fitted) return false;
        candidate.count = 1;
        out = candidate;
        return true;
    }
    CameraFrustum frustum;
    if (!cameraFrustum(settings, shadows.maxDistance > 0, frustum)) return false;
    const double nearDistance = frustum.nearDistance;
    const double farDistance =
        shadows.maxDistance > 0 ? std::min(frustum.farDistance, double(shadows.maxDistance)) : frustum.farDistance;
    if (!(farDistance > nearDistance)) return false;
    double previous = nearDistance;
    for (std::uint32_t i = 0; i < shadows.cascadeCount; ++i) {
        const double fraction = double(i + 1) / shadows.cascadeCount;
        const double linear = nearDistance + (farDistance - nearDistance) * fraction;
        const double logarithmic = nearDistance * std::pow(farDistance / nearDistance, fraction);
        const double split = i + 1 == shadows.cascadeCount ? farDistance
                                                           : (1 - double(shadows.splitLambda)) * linear +
                                                                 double(shadows.splitLambda) * logarithmic;
        const float end = static_cast<float>(split);
        if (!std::isfinite(end) || !(end > previous)) return false;
        candidate.splits[i] = end;
        const double blendFraction =
            i + 1 < shadows.cascadeCount ? shadows.blendFraction : shadows.distanceFadeFraction;
        candidate.blendStarts[i] = static_cast<float>(end - blendFraction * (end - previous));
        if (!std::isfinite(candidate.blendStarts[i]) || candidate.blendStarts[i] < previous ||
            candidate.blendStarts[i] > end)
            return false;
        Corners slice;
        const double first = i == 0 ? nearDistance : candidate.blendStarts[i - 1];
        if (!sliceCorners(frustum, first, end, slice) ||
            !cascadeProjection(slice, casters, basis, shadows, candidate.matrices[i]))
            return false;
        previous = end;
    }
    candidate.count = shadows.cascadeCount;
    out = candidate;
    return true;
}
}
