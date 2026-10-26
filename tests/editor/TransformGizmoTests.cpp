#include <Transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace {
using aeg::editor::pivotMatrix;
using aeg::editor::Transform;
using aeg::editor::tryApplyPivotMatrix;
using aeg::math::Mat4;
using aeg::math::Vec3;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// Independent analytic Rx*Ry*Rz coefficients, with each column scaled. This
// deliberately avoids Transform::matrix(), quaternion construction and TRS helpers.
Mat4 referenceMatrix(Vec3 translation, Vec3 rotation, Vec3 scale) {
    const double factor = std::numbers::pi / 180;
    const double x = rotation.x * factor, y = rotation.y * factor, z = rotation.z * factor;
    const double sx = std::sin(x), cx = std::cos(x), sy = std::sin(y), cy = std::cos(y);
    const double sz = std::sin(z), cz = std::cos(z);
    const double rows[3][3]{{cy * cz, -cy * sz, sy},
                            {cx * sz + sx * sy * cz, cx * cz - sx * sy * sz, -sx * cy},
                            {sx * sz - cx * sy * cz, sx * cz + cx * sy * sz, cx * cy}};
    Mat4 result = aeg::math::identity();
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column)
            result.at(row, column) = static_cast<float>(rows[row][column] * scale[column]);
        result.at(row, 3) = translation[row];
    }
    return result;
}

Mat4 referencePivot(Mat4 model, Vec3 localPivot) {
    for (std::size_t row = 0; row < 3; ++row) {
        double value = model.at(row, 3);
        for (std::size_t column = 0; column < 3; ++column) value += model.at(row, column) * localPivot[column];
        model.at(row, 3) = static_cast<float>(value);
    }
    return model;
}

bool matrixClose(const Mat4& actual, const Mat4& expected, float tolerance = 0.0003f) {
    for (std::size_t column = 0; column < 4; ++column) {
        float magnitude = 1;
        for (std::size_t row = 0; row < 4; ++row) magnitude = std::max(magnitude, std::abs(expected.at(row, column)));
        for (std::size_t row = 0; row < 4; ++row)
            if (!std::isfinite(actual.at(row, column)) ||
                std::abs(actual.at(row, column) - expected.at(row, column)) > tolerance * magnitude)
                return false;
    }
    return true;
}

bool sameTransform(const Transform& a, const Transform& b) {
    return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}

void matrixRecoveryPreservesEngineEulerOrder() {
    const Vec3 angles{31, -47, 73}, position{5, -6, 7}, scale{2, 3, 4}, center{1, 2, -3};
    const auto expected = referenceMatrix(position, angles, scale);
    Transform previous{position, {30, -46, 72}, scale}, result;
    require(tryApplyPivotMatrix(referencePivot(expected, center), center, previous, result),
            "A valid centered TRS matrix must be accepted");
    require(matrixClose(result.matrix(), expected), "Recovered TRS must preserve noncommuting Rx*Ry*Rz order");
    require(aeg::math::nearly_equal(result.translation, position, 0.0001f),
            "Recover model translation from the centered pivot, not the pivot translation");
}

void centeredPivotUsesTheTransformedModelCenter() {
    const Transform transform{{5, -6, 7}, {0, 0, 90}, {-2, 3, 4}};
    const auto pivot = pivotMatrix(transform, {1, 2, -3});
    require(std::abs(pivot.at(0, 3) + 1) < 0.00001f && std::abs(pivot.at(1, 3) + 8) < 0.00001f &&
                std::abs(pivot.at(2, 3) + 5) < 0.00001f,
            "Rotated, scaled local center must appear at world(-1,-8,-5)");
    require(matrixClose(pivot, referencePivot(referenceMatrix({5, -6, 7}, {0, 0, 90}, {-2, 3, 4}), {1, 2, -3})),
            "Centered pivot must preserve the entire model basis");
}

void matrixRoundTripsPreserveSignedNonuniformScale() {
    constexpr std::array<Vec3, 8> angles{{{0, 0, 0},
                                          {25, -37, 81},
                                          {-75, 105, 179},
                                          {40, 90, -17},
                                          {-24, -90, 38},
                                          {12, 89.99f, -43},
                                          {45, -89.99f, 125},
                                          {720.5f, -123, -541}}};
    for (unsigned signs = 0; signs < 8; ++signs) {
        for (const auto rotation : angles) {
            for (const Vec3 magnitudes : {Vec3{1.2f, 2.3f, 4}, Vec3{0.001f, 1000, 0.7f}}) {
                const Vec3 scale{signs & 1 ? -magnitudes.x : magnitudes.x, signs & 2 ? -magnitudes.y : magnitudes.y,
                                 signs & 4 ? -magnitudes.z : magnitudes.z};
                const Transform previous{{-2, 4, 6}, rotation, scale};
                const auto expected = referenceMatrix(previous.translation, rotation, scale);
                const Vec3 center{1.3f, -2.1f, 3.2f};
                Transform result;
                require(tryApplyPivotMatrix(referencePivot(expected, center), center, previous, result),
                        "Finite TRS including signed nonuniform boundary scales must round-trip");
                require(matrixClose(result.matrix(), expected), "Round-trip changed the model transform");
                for (unsigned axis = 0; axis < 3; ++axis)
                    require(std::signbit(result.scale[axis]) == std::signbit(scale[axis]),
                            "Matrix edits must preserve the reference reflection signs");
            }
        }
    }
}

void wrappedAndAlternateEulerBranchesStayNearThePreviousAngles() {
    for (const auto [previousRotation, nextRotation] :
         {std::pair{Vec3{12, 22, 179}, Vec3{12, 22, 181}}, std::pair{Vec3{21, 100, -32}, Vec3{22, 101, -31}},
          std::pair{Vec3{719, -123, -541}, Vec3{721, -124, -539}}}) {
        const Transform previous{{1, 2, 3}, previousRotation, {1, 2, 3}};
        Transform result;
        const auto expected = referenceMatrix({1, 2, 3}, nextRotation, {1, 2, 3});
        require(tryApplyPivotMatrix(referencePivot(expected, {2, -1, 4}), {2, -1, 4}, previous, result),
                "Wrapped or alternate Euler branch is a valid edit");
        require(matrixClose(result.matrix(), expected), "Euler continuity changed the requested rotation");
        for (unsigned axis = 0; axis < 3; ++axis)
            require(std::abs(result.rotation[axis] - previousRotation[axis]) < 4,
                    "A small rotation edit must not jump to another Euler branch");
    }
}

void gimbalLockRetainsANearbyEquivalentRepresentation() {
    for (const float pole : {-90.0f, 90.0f}) {
        const Transform previous{{3, -4, 5}, {40, pole, -17}, {-2, 3, -4}};
        Transform result;
        const auto expected = referenceMatrix({3, -4, 5}, {42, pole, -17}, {-2, 3, -4});
        require(tryApplyPivotMatrix(referencePivot(expected, {1, 2, 3}), {1, 2, 3}, previous, result),
                "An exact gimbal-lock rotation must remain editable");
        require(matrixClose(result.matrix(), expected), "Gimbal-lock recovery must reproduce the requested matrix");
        require(std::abs(result.rotation.x - previous.rotation.x) < 3 &&
                    std::abs(result.rotation.z - previous.rotation.z) < 3,
                "Gimbal lock must retain a nearby equivalent Euler representation");
    }
}

void outputMayAliasTheReferenceTransform() {
    Transform edited{{-3, 7, 11}, {25, -37, 73}, {-2, 3, -4}};
    const auto expected = referenceMatrix({5, -6, 7}, {27, -39, 75}, {-2, 3, -4});
    require(tryApplyPivotMatrix(referencePivot(expected, {1, 2, 3}), {1, 2, 3}, edited, edited),
            "A transform may be used as both the reference and output");
    require(matrixClose(edited.matrix(), expected), "Aliased recovery must preserve the requested matrix");
    auto invalid = expected;
    invalid.at(0, 0) = std::numeric_limits<float>::quiet_NaN();
    const auto saved = edited;
    require(!tryApplyPivotMatrix(invalid, {1, 2, 3}, edited, edited) && sameTransform(edited, saved),
            "A failed edit must preserve an aliased reference/output");
}

void invalidEditsPreserveTheOutput() {
    const Transform previous{{1, 2, 3}, {25, 35, 45}, {-2, 3, -4}};
    const Transform sentinel{{-71, 17, 99}, {-123, 456, -789}, {-9, 8, -7}};
    const Vec3 pivot{1, 2, 3};
    const auto valid = referencePivot(referenceMatrix(previous.translation, previous.rotation, previous.scale), pivot);
    std::array<Mat4, 8> invalid;
    invalid.fill(valid);
    invalid[0].at(0, 1) += 1;
    invalid[1].at(3, 0) = 0.1f;
    invalid[2].at(3, 3) = 0;
    invalid[3].at(1, 2) = std::numeric_limits<float>::quiet_NaN();
    invalid[4].at(0, 3) = std::numeric_limits<float>::infinity();
    for (unsigned row = 0; row < 3; ++row) invalid[5].at(row, 0) = 0;
    invalid[6] = referencePivot(referenceMatrix({1, 2, 3}, {25, 35, 45}, {-0.0001f, 3, -4}), pivot);
    invalid[7] = referencePivot(referenceMatrix({1, 2, 3}, {25, 35, 45}, {-2000, 3, -4}), pivot);
    for (const auto& matrix : invalid) {
        auto out = sentinel;
        require(!tryApplyPivotMatrix(matrix, pivot, previous, out), "Invalid, sheared or singular edit must fail");
        require(sameTransform(out, sentinel), "Failed matrix edit must leave output parameters unchanged");
    }
    auto out = sentinel;
    require(!tryApplyPivotMatrix(valid, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, previous, out) &&
                sameTransform(out, sentinel),
            "Nonfinite pivot must fail transactionally");
    auto invalidReference = previous;
    invalidReference.rotation.x = std::numeric_limits<float>::infinity();
    require(!tryApplyPivotMatrix(valid, pivot, invalidReference, out) && sameTransform(out, sentinel),
            "Nonfinite reference must fail transactionally");
    const Transform largeScale{{0, 0, 0}, {0, 0, 0}, {1000, 1000, 1000}};
    const auto finitePivot = referenceMatrix({0, 0, 0}, {0, 0, 0}, {1000, 1000, 1000});
    require(!tryApplyPivotMatrix(finitePivot, {std::numeric_limits<float>::max(), 0, 0}, largeScale, out) &&
                sameTransform(out, sentinel),
            "Finite matrix and pivot whose recovered translation overflows float must fail transactionally");
}
}

int main() {
    const std::pair<const char*, std::function<void()>> tests[]{
        {"recover centered Rx*Ry*Rz", matrixRecoveryPreservesEngineEulerOrder},
        {"pivot follows transformed model center", centeredPivotUsesTheTransformedModelCenter},
        {"preserve signed nonuniform scale", matrixRoundTripsPreserveSignedNonuniformScale},
        {"Euler wrap and alternate branch continuity", wrappedAndAlternateEulerBranchesStayNearThePreviousAngles},
        {"gimbal-lock continuity", gimbalLockRetainsANearbyEquivalentRepresentation},
        {"aliased reference/output", outputMayAliasTheReferenceTransform},
        {"transactional invalid edits", invalidEditsPreserveTheOutput}};
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
