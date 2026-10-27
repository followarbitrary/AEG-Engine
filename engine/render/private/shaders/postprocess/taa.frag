#version 450
#extension GL_GOOGLE_include_directive : require

#include "../common/hdr.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(location = 1) out float outDepth;
layout(set = 0, binding = 0) uniform sampler2D currentColor;
layout(set = 0, binding = 1) uniform sampler2D motionDepth;
layout(set = 0, binding = 2) uniform sampler2D historyColor;
layout(set = 0, binding = 3) uniform sampler2D historyDepth;
layout(push_constant) uniform TemporalParameters {
    layout(offset = 0) vec2 inverseResolution;
    layout(offset = 8) float historyWeight;
    layout(offset = 12) uint historyValid;
}
temporal;

void main() {
    ivec2 resolution = textureSize(currentColor, 0);
    ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), resolution - 1);
    vec3 current = finiteHdrRadiance(texelFetch(currentColor, pixel, 0).rgb);
    vec3 minimumColor = current;
    vec3 maximumColor = current;
    vec4 selectedMotion = texelFetch(motionDepth, pixel, 0);
    outColor = vec4(current, 1.0);
    if (selectedMotion.w < -1.5) {
        outDepth = -1.0;
        return;
    }
    bool foreground = selectedMotion.w >= 0.0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 neighbor = clamp(pixel + ivec2(x, y), ivec2(0), resolution - 1);
            vec3 color = finiteHdrRadiance(texelFetch(currentColor, neighbor, 0).rgb);
            minimumColor = min(minimumColor, color);
            maximumColor = max(maximumColor, color);
            vec4 motion = texelFetch(motionDepth, neighbor, 0);
            if (motion.w < -1.5) {
                // Keep current transparent composition and prevent stale opaque
                // history from leaking through jittered silhouette neighbors.
                outDepth = -1.0;
                return;
            }
            // A neighboring foreground sample preserves jittered silhouette coverage.
            if (motion.w >= 0.0 && (!foreground || motion.w < selectedMotion.w)) {
                selectedMotion = motion;
                foreground = true;
            }
        }
    }

    outDepth = foreground ? selectedMotion.w : -1.0;
    if (!foreground || temporal.historyValid == 0 || selectedMotion.z < 0.0) return;

    // Motion excludes projection jitter; the resolve grid itself is unjittered.
    vec2 historyUV = uv - selectedMotion.xy;
    if (any(lessThan(historyUV, vec2(0.0))) || any(greaterThanEqual(historyUV, vec2(1.0)))) return;

    ivec2 historyPixel = clamp(ivec2(historyUV / temporal.inverseResolution), ivec2(0), resolution - 1);
    float storedDepth = texelFetch(historyDepth, historyPixel, 0).r;
    float expectedDepth = selectedMotion.z;
    float depthTolerance = 0.01 * max(abs(storedDepth), abs(expectedDepth)) + 1e-4;
    if (storedDepth < 0.0 || abs(storedDepth - expectedDepth) > depthTolerance) return;

    // Only historical RGB is filtered. Current samples stay raw for temporal accumulation.
    vec3 history = finiteHdrRadiance(textureLod(historyColor, historyUV, 0.0).rgb);
    history = clamp(history, minimumColor, maximumColor);
    outColor = vec4(finiteHdrRadiance(mix(current, history, clamp(temporal.historyWeight, 0.0, 0.9))), 1.0);
}
