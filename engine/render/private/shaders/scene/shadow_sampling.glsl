layout(set = 2, binding = 0) uniform sampler2D shadowTexture;

// Called at the beginning of main, before alpha discard or varying light branches.
vec2 shadowDepthGradient(vec3 worldPosition) {
    vec4 clip = scene.lightViewProjection * vec4(worldPosition, 1.0);
    vec3 ndc = clip.xyz / (clip.w != 0.0 ? clip.w : 1.0);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    // A sloped receiver has a different depth at each PCF texel. Comparing every
    // tap to the center depth makes neighboring parts of that plane self-shadow.
    vec2 uvDx = dFdx(uv), uvDy = dFdy(uv);
    float depthDx = dFdx(ndc.z), depthDy = dFdy(ndc.z);
    float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
    vec2 depthGradient = vec2(0.0);
    if (abs(determinant) > 1e-20) {
        depthGradient = vec2(depthDx * uvDy.y - depthDy * uvDx.y, uvDx.x * depthDy - uvDy.x * depthDx) / determinant;
        if (any(isnan(depthGradient)) || any(isinf(depthGradient))) depthGradient = vec2(0.0);
    }
    return depthGradient;
}

float shadowVisibility(vec3 worldPosition, vec3 geometricNormal, vec3 towardLight, uint lightIndex,
                       vec2 depthGradient) {
    if (scene.shadowInfo.x < 0.5 || float(lightIndex) != scene.shadowInfo.y) return 1.0;
    vec4 clip = scene.lightViewProjection * vec4(worldPosition, 1.0);
    if (any(isnan(clip)) || any(isinf(clip)) || clip.w <= 0.0) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    if (any(isnan(ndc)) || any(isinf(ndc))) return 1.0;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;
    float normalLength2 = dot(geometricNormal, geometricNormal);
    float lightLength2 = dot(towardLight, towardLight);
    float noL = normalLength2 > 0.0 && lightLength2 > 0.0
                    ? clamp(dot(geometricNormal * inversesqrt(normalLength2), towardLight * inversesqrt(lightLength2)),
                            0.0, 1.0)
                    : 0.0;
    float receiverDepth = ndc.z - scene.shadowBias.x - scene.shadowBias.y * (1.0 - noL);
    float texel = scene.shadowInfo.z;
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 tap = uv + vec2(x, y) * texel;
            // Clamp-to-edge must not turn taps outside the light frustum into occluders.
            if (any(lessThan(tap, vec2(0.0))) || any(greaterThan(tap, vec2(1.0))))
                visibility += 1.0;
            else {
                // Nearest sampling reads the texel center, which can be half a
                // texel away from tap. Correct the comparison at that exact point.
                vec2 tapCenter = clamp((floor(tap / texel) + 0.5) * texel, vec2(texel * 0.5), vec2(1.0 - texel * 0.5));
                float tapReceiverDepth = receiverDepth + dot(depthGradient, tapCenter - uv);
                visibility += tapReceiverDepth <= textureLod(shadowTexture, tap, 0.0).r ? 1.0 : 0.0;
            }
        }
    }
    return visibility / 9.0;
}
