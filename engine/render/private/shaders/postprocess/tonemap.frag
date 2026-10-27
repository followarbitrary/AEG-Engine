#version 450
#extension GL_GOOGLE_include_directive : require

#include "../common/hdr.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D hdrColor;
layout(push_constant) uniform ToneParameters {
    layout(offset = 0) uint mapper;
    layout(offset = 4) float exposureEV;
    layout(offset = 8) vec2 padding;
}
tone;

// Stephen Hill's ACES RRT+ODT fitted approximation, adapted from BakingLab.
// Copyright (c) 2016 MJP. MIT license and Hill attribution: ACES.LICENSE.
// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
// Input/output use linear-light sRGB/Rec.709 primaries. GLSL constructors list columns.
vec3 acesFitted(vec3 color) {
    const mat3 inputMat = mat3(
        0.59719, 0.07600, 0.02840,
        0.35458, 0.90834, 0.13383,
        0.04823, 0.01566, 0.83777
    );
    const mat3 outputMat = mat3(
        1.60475, -0.10208, -0.00327,
        -0.53108, 1.10813, -0.07276,
        -0.07367, -0.00605, 1.07602
    );
    vec3 v = inputMat * color;
    vec3 fitted = (v * (v + 0.0245786) - 0.000090537) /
                  (v * (0.983729 * v + 0.4329510) + 0.238081);
    return clamp(outputMat * fitted, vec3(0.0), vec3(1.0));
}

void main() {
    // Validated EV in [-12,12] keeps the rational fit finite for the working HDR range.
    vec3 exposed = finiteHdrRadiance(textureLod(hdrColor, uv, 0.0).rgb) * exp2(tone.exposureEV);
    vec3 mapped = tone.mapper == 0u ? clamp(exposed, vec3(0.0), vec3(1.0)) : acesFitted(exposed);
    // Presentation and capture encode sRGB once after this display-linear output.
    outColor = vec4(mapped, 1.0);
}
