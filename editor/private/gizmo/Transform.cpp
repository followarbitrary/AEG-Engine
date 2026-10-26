#include "Transform.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace aeg::editor {
namespace {
constexpr double minimumScale = 0.001, maximumScale = 1000;
constexpr double basisTolerance = 0.0002;
constexpr double degreesPerRadian = 180 / std::numbers::pi;

bool validReference(const Transform& transform) {
    if (!math::is_finite(transform.translation) || !math::is_finite(transform.rotation) ||
        !math::is_finite(transform.scale))
        return false;
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (std::abs(transform.scale[axis]) < minimumScale || std::abs(transform.scale[axis]) > maximumScale)
            return false;
    return true;
}

double nearestDegrees(double degrees, double reference) {
    return reference + std::remainder(degrees - reference, 360.0);
}

math::Vec3 rotationDegrees(const double (&rotation)[3][3], math::Vec3 previous) {
    const double cosineY = std::hypot(rotation[0][0], rotation[0][1]);
    const double y = std::atan2(rotation[0][2], cosineY);
    if (cosineY <= 0.000001) {
        // At +90 degrees x+z is observable; at -90 degrees x-z is observable.
        // Share that change between x and z to minimize distance to the previous
        // angles instead of abruptly forcing either angle to zero.
        const double sign = std::copysign(1.0, rotation[0][2]);
        const double coupled = std::atan2(sign * rotation[1][0], rotation[1][1]) * degreesPerRadian;
        const double previousCoupled = static_cast<double>(previous.x) + sign * previous.z;
        const double delta = std::remainder(coupled - previousCoupled, 360.0);
        return {static_cast<float>(previous.x + delta * 0.5), static_cast<float>(nearestDegrees(sign * 90, previous.y)),
                static_cast<float>(previous.z + sign * delta * 0.5)};
    }

    const double x = std::atan2(-rotation[1][2], rotation[2][2]);
    const double z = std::atan2(-rotation[0][1], rotation[0][0]);
    const std::array<std::array<double, 3>, 2> branches{
        {{x * degreesPerRadian, y * degreesPerRadian, z * degreesPerRadian},
         {x * degreesPerRadian + 180, 180 - y * degreesPerRadian, z * degreesPerRadian + 180}}};
    math::Vec3 best;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (const auto& branch : branches) {
        math::Vec3 candidate;
        double distance = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double value = nearestDegrees(branch[axis], previous[axis]);
            candidate[axis] = static_cast<float>(value);
            const double difference = value - previous[axis];
            distance += difference * difference;
        }
        if (distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

bool sameBasis(const math::Mat4& recomposed, const math::Mat4& input) {
    if (!math::is_finite(recomposed)) return false;
    for (std::size_t column = 0; column < 3; ++column) {
        const double magnitude =
            std::hypot(static_cast<double>(input.at(0, column)), static_cast<double>(input.at(1, column)),
                       static_cast<double>(input.at(2, column)));
        for (std::size_t row = 0; row < 3; ++row)
            if (std::abs(static_cast<double>(recomposed.at(row, column)) - input.at(row, column)) >
                magnitude * basisTolerance)
                return false;
    }
    return true;
}
}

math::Mat4 pivotMatrix(const Transform& transform, math::Vec3 localPivot) {
    if (!math::is_finite(localPivot)) return {};
    const auto result = transform.matrix() * math::translation(localPivot);
    return math::is_finite(result) ? result : math::Mat4{};
}

bool tryApplyPivotMatrix(const math::Mat4& pivot, math::Vec3 localPivot, const Transform& reference, Transform& out) {
    if (!math::is_finite(pivot) || !math::is_finite(localPivot) || !validReference(reference)) return false;
    if (std::abs(pivot.at(3, 0)) > 0.000001f || std::abs(pivot.at(3, 1)) > 0.000001f ||
        std::abs(pivot.at(3, 2)) > 0.000001f || std::abs(pivot.at(3, 3) - 1) > 0.000001f)
        return false;

    Transform result;
    double rotation[3][3];
    for (std::size_t column = 0; column < 3; ++column) {
        const double magnitude =
            std::hypot(static_cast<double>(pivot.at(0, column)), static_cast<double>(pivot.at(1, column)),
                       static_cast<double>(pivot.at(2, column)));
        // Allow only the rounding error of a boundary scale multiplied by a
        // float rotation. Published scales always obey the hard editor limits.
        if (magnitude < minimumScale * (1 - 0.000002) || magnitude > maximumScale * (1 + 0.000002)) return false;
        const double signedMagnitude = std::copysign(magnitude, reference.scale[column]);
        result.scale[column] =
            static_cast<float>(std::copysign(std::clamp(magnitude, minimumScale, maximumScale), signedMagnitude));
        for (std::size_t row = 0; row < 3; ++row) rotation[row][column] = pivot.at(row, column) / signedMagnitude;
    }
    for (std::size_t a = 0; a < 3; ++a)
        for (std::size_t b = a + 1; b < 3; ++b) {
            double product = 0;
            for (std::size_t row = 0; row < 3; ++row) product += rotation[row][a] * rotation[row][b];
            if (std::abs(product) > basisTolerance) return false;
        }
    const double determinant = rotation[0][0] * (rotation[1][1] * rotation[2][2] - rotation[1][2] * rotation[2][1]) -
                               rotation[0][1] * (rotation[1][0] * rotation[2][2] - rotation[1][2] * rotation[2][0]) +
                               rotation[0][2] * (rotation[1][0] * rotation[2][1] - rotation[1][1] * rotation[2][0]);
    if (std::abs(determinant - 1) > basisTolerance * 2) return false;

    result.rotation = rotationDegrees(rotation, reference.rotation);
    if (!math::is_finite(result.rotation)) return false;
    for (std::size_t row = 0; row < 3; ++row) {
        double translation = pivot.at(row, 3);
        for (std::size_t column = 0; column < 3; ++column)
            translation -= static_cast<double>(pivot.at(row, column)) * localPivot[column];
        if (!std::isfinite(translation) || std::abs(translation) > std::numeric_limits<float>::max()) return false;
        result.translation[row] = static_cast<float>(translation);
    }
    if (!math::is_finite(result.translation) || !sameBasis(result.matrix(), pivot)) return false;
    out = result;
    return true;
}
}
