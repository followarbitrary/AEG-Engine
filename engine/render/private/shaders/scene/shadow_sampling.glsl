layout(set = 2, binding = 0) uniform sampler2D shadowTexture0;
layout(set = 2, binding = 1) uniform sampler2D shadowTexture1;
layout(set = 2, binding = 2) uniform sampler2D shadowTexture2;
layout(set = 2, binding = 3) uniform sampler2D shadowTexture3;

struct ShadowGradients {
    vec2 values[4];
};

// Called at the beginning of main, before alpha discard or varying branches.
// Derivatives must not straddle a per-pixel change in the selected projection.
ShadowGradients shadowDepthGradients(vec3 worldPosition) {
    ShadowGradients result;
    for (int cascade = 0; cascade < 4; ++cascade) {
        result.values[cascade] = vec2(0.0);
        // This guard is uniform across the draw, so active derivatives remain coherent.
        if (scene.shadowInfo.x < 0.5 || float(cascade) >= scene.shadowInfo.w) continue;
        vec4 clip = scene.lightViewProjections[cascade] * vec4(worldPosition, 1.0);
        vec3 ndc = clip.xyz / (clip.w != 0.0 ? clip.w : 1.0);
        vec2 uv = ndc.xy * 0.5 + 0.5;
        // Receiver-plane correction compares each tap at its own plane depth.
        vec2 uvDx = dFdx(uv), uvDy = dFdy(uv);
        float depthDx = dFdx(ndc.z), depthDy = dFdy(ndc.z);
        float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
        vec2 gradient = vec2(0.0);
        if (abs(determinant) > 1e-20) {
            gradient = vec2(depthDx * uvDy.y - depthDy * uvDx.y, uvDx.x * depthDy - uvDy.x * depthDx) / determinant;
            if (any(isnan(gradient)) || any(isinf(gradient))) gradient = vec2(0.0);
        }
        result.values[cascade] = gradient;
    }
    return result;
}

// Separate, statically named samplers avoid sampled-image array indexing features.
float shadowMapDepth(int cascade, vec2 uv) {
    if (cascade == 0) return textureLod(shadowTexture0, uv, 0.0).r;
    if (cascade == 1) return textureLod(shadowTexture1, uv, 0.0).r;
    if (cascade == 2) return textureLod(shadowTexture2, uv, 0.0).r;
    return textureLod(shadowTexture3, uv, 0.0).r;
}

float cascadeVisibility(int cascade, vec3 worldPosition, float bias, vec2 depthGradient) {
    vec4 clip = scene.lightViewProjections[cascade] * vec4(worldPosition, 1.0);
    if (any(isnan(clip)) || any(isinf(clip)) || clip.w <= 0.0) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    if (any(isnan(ndc)) || any(isinf(ndc))) return 1.0;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;
    float receiverDepth = ndc.z - bias;
    float texel = scene.shadowInfo.z;
    int radius = clamp(int(scene.shadowBias.z), 0, 2);
    float visibility = 0.0;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            vec2 tap = uv + vec2(x, y) * texel;
            // Clamp-to-edge must not turn taps outside the light frustum into occluders.
            if (any(lessThan(tap, vec2(0.0))) || any(greaterThan(tap, vec2(1.0))))
                visibility += 1.0;
            else {
                // Nearest sampling reads the texel center, up to half a texel from tap.
                vec2 tapCenter = clamp((floor(tap / texel) + 0.5) * texel, vec2(texel * 0.5), vec2(1.0 - texel * 0.5));
                float tapReceiverDepth = receiverDepth + dot(depthGradient, tapCenter - uv);
                visibility += tapReceiverDepth <= shadowMapDepth(cascade, tap) ? 1.0 : 0.0;
            }
        }
    }
    float diameter = float(radius * 2 + 1);
    return visibility / (diameter * diameter);
}

float shadowVisibility(vec3 worldPosition, vec3 geometricNormal, vec3 towardLight, uint lightIndex,
                       ShadowGradients gradients) {
    if (scene.shadowInfo.x < 0.5 || float(lightIndex) != scene.shadowInfo.y) return 1.0;
    float normalLength2 = dot(geometricNormal, geometricNormal);
    float lightLength2 = dot(towardLight, towardLight);
    float noL = normalLength2 > 0.0 && lightLength2 > 0.0
                    ? clamp(dot(geometricNormal * inversesqrt(normalLength2), towardLight * inversesqrt(lightLength2)),
                            0.0, 1.0)
                    : 0.0;
    float bias = scene.shadowBias.x + scene.shadowBias.y * (1.0 - noL);
    int count = clamp(int(scene.shadowInfo.w), 1, 4);
    // Single directional fitting and Spot keep their original complete-map policy.
    if (count == 1) return cascadeVisibility(0, worldPosition, bias, gradients.values[0]);

    float depth = dot(worldPosition - scene.cameraPosition.xyz, -scene.cameraTowardViewer.xyz);
    if (isnan(depth) || isinf(depth) || depth < 0.0 || depth > scene.cascadeSplits[count - 1]) return 1.0;
    int cascade = 0;
    while (cascade < count - 1 && depth > scene.cascadeSplits[cascade]) ++cascade;
    float visibility = cascadeVisibility(cascade, worldPosition, bias, gradients.values[cascade]);
    float start = scene.cascadeBlendStarts[cascade], end = scene.cascadeSplits[cascade];
    if (end > start && depth > start) {
        float nextVisibility = cascade < count - 1
                                   ? cascadeVisibility(cascade + 1, worldPosition, bias, gradients.values[cascade + 1])
                                   : 1.0;
        visibility = mix(visibility, nextVisibility, smoothstep(start, end, depth));
    }
    return visibility;
}
