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
// AgX fitted curve adapted from Google's Filament AgxToneMapper (Apache-2.0),
// which cites Blender's AgX LUT generation and the minimal fit at
// https://iolite-engine.com/blog_posts/minimal_agx_implementation. The fit
// operates on linear sRGB/Rec.709 radiance and returns linear sRGB values.
// https://github.com/google/filament/blob/main/filament/src/ToneMapper.cpp
vec3 agxFitted(vec3 color) {
    // Filament's AgX fit is defined in linear Rec.2020. Convert the engine's
    // linear Rec.709/sRGB scene values into that working primary space first,
    // then convert the display-linear result back to Rec.709.
    const mat3 srgbToRec2020 = mat3(
        0.6274, 0.0691, 0.0164,
        0.3293, 0.9195, 0.0880,
        0.0433, 0.0114, 0.8956
    );
    const mat3 rec2020ToSrgb = mat3(
        1.6605, -0.1246, -0.0182,
        -0.5876, 1.1329, -0.1006,
        -0.0728, -0.0083, 1.1187
    );
    const mat3 inputMat = mat3(
        0.856627153315983, 0.137318972929847, 0.111898212999950,
        0.095121240538159, 0.761241990602591, 0.076799418603190,
        0.048251606145858, 0.101439036467562, 0.811302368396859
    );
    const mat3 outputMat = mat3(
        1.127100581814436, -0.141329763498438, -0.141329763498438,
        -0.110606643096603, 1.157823702216272, -0.110606643096603,
        -0.016493938338833, -0.016493938338833, 1.251936406595040
    );
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    color = max(color, vec3(0.0));
    color = srgbToRec2020 * color;
    color = inputMat * color;
    color = max(log2(max(color, vec3(1e-10))), vec3(minEv));
    color = clamp((color - minEv) / (maxEv - minEv), vec3(0.0), vec3(1.0));

    vec3 x2 = color * color;
    vec3 x4 = x2 * x2;
    vec3 x6 = x4 * x2;
    color = -17.86 * x6 * color + 78.01 * x6 - 126.7 * x4 * color + 92.06 * x4 -
            28.72 * x2 * color + 4.361 * x2 - 0.1718 * color + 0.002857;
    color = pow(max(outputMat * color, vec3(0.0)), vec3(2.2));
    return clamp(rec2020ToSrgb * color, vec3(0.0), vec3(1.0));
}

void main() {
    // Validated EV in [-12,12] keeps the rational fit finite for the working HDR range.
    vec3 exposed = finiteHdrRadiance(textureLod(hdrColor, uv, 0.0).rgb) * exp2(tone.exposureEV);
    vec3 mapped = tone.mapper == 0u ? clamp(exposed, vec3(0.0), vec3(1.0))
                                   : tone.mapper == 2u ? agxFitted(exposed) : acesFitted(exposed);
    // Presentation and capture encode sRGB once after this display-linear output.
    outColor = vec4(mapped, 1.0);
}
