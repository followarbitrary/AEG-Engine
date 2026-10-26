#pragma once
#include <math.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace aeg::tests {
// Deliberately uses double precision and the Lambda definition of Smith G,
// independently from the shader's correlated visibility implementation.
inline std::array<double, 3> pbrReference(math::Vec3 base, float metallic, float roughness, math::Vec3 n,
                                          math::Vec3 towardLight, math::Vec3 towardViewer,
                                          math::Vec3 radiance = {3, 3, 3}, float ambient = 0) {
    using V = std::array<double, 3>;
    const auto normalized = [](math::Vec3 v) {
        const double length = std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z);
        return V{v.x / length, v.y / length, v.z / length};
    };
    const auto dot = [](V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const auto normal = normalized(n), light = normalized(towardLight), view = normalized(towardViewer);
    const double noL = std::clamp(dot(normal, light), 0.0, 1.0);
    const double noV = std::clamp(dot(normal, view), 0.0, 1.0);
    const V color{base.x, base.y, base.z}, incident{radiance.x, radiance.y, radiance.z};
    V result{};
    for (unsigned i = 0; i < 3; ++i) result[i] = ambient * color[i] * (1.0 - metallic);
    if (noL <= 0 || noV <= 0) return result;
    V half{light[0] + view[0], light[1] + view[1], light[2] + view[2]};
    const double halfLength = std::sqrt(dot(half, half));
    if (halfLength < 1e-12) return result;
    for (auto& component : half) component /= halfLength;
    const double noH = std::clamp(dot(normal, half), 0.0, 1.0);
    const double voH = std::clamp(dot(view, half), 0.0, 1.0);
    const double perceptual = std::clamp(double(roughness), 0.045, 1.0);
    const double alpha = perceptual * perceptual, alphaSquared = alpha * alpha;
    const double denominator = 1.0 + noH * noH * (alphaSquared - 1.0);
    const double distribution = alphaSquared / (std::numbers::pi * denominator * denominator);
    const auto lambda = [&](double cosine) {
        return (std::sqrt(1.0 + alphaSquared * (1.0 - cosine * cosine) / (cosine * cosine)) - 1.0) * 0.5;
    };
    const double geometry = 1.0 / (1.0 + lambda(noL) + lambda(noV));
    for (unsigned i = 0; i < 3; ++i) {
        const double f0 = 0.04 * (1.0 - metallic) + color[i] * metallic;
        const double fresnel = f0 + (1.0 - f0) * std::pow(1.0 - voH, 5.0);
        const double diffuse = (1.0 - fresnel) * (1.0 - metallic) * color[i] / std::numbers::pi;
        const double specular = distribution * geometry * fresnel / (4.0 * noL * noV);
        result[i] += (diffuse + specular) * incident[i] * noL;
    }
    return result;
}
}
