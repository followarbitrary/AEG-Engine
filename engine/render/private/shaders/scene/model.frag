#version 450
#extension GL_GOOGLE_include_directive : require
layout(location = 0) in vec3 normal;
layout(location = 1) in vec2 uv;
layout(location = 4) in vec4 tangentFrame;
layout(location = 0) out vec4 outColor;
#ifdef AEG_TAA
layout(location = 2) in vec4 currentClip;
layout(location = 3) in vec4 previousClip;
layout(location = 1) out vec4 outMotionDepth;
#endif
layout(set = 0, binding = 0) uniform Scene {
    mat4 viewProjection;
    mat4 model;
    vec4 lightDirection;
    vec4 parameters;
    vec4 clipConvention;
#ifdef AEG_TAA
    mat4 previousViewProjection;
    mat4 previousModel;
    mat4 currentViewProjection;
    vec4 temporalInfo;
#endif
}
scene;
layout(set = 1, binding = 0) uniform sampler2D baseTexture;
layout(set = 1, binding = 1) uniform sampler2D normalTexture;
layout(push_constant) uniform Material {
    vec4 baseColor;
    float alphaCutoff;
    uint alphaMasked;
    float normalScale;
    uint hasNormalMap;
}
material;
#include "normal_mapping.glsl"
void main() {
    vec4 base = texture(baseTexture, uv) * material.baseColor;
    if (material.alphaMasked != 0 && base.a < material.alphaCutoff) discard;
    vec3 n = normalize(normal);
    if (material.hasNormalMap != 0 && scene.parameters.z > 0.5 &&
        scene.parameters.w > 0.0 && scene.parameters.y < 0.5)
        n = applyNormalMap(n, tangentFrame, texture(normalTexture, uv).rgb,
                           material.normalScale, scene.parameters.w);
    if (!gl_FrontFacing) n = -n;
    float diffuse = max(dot(n, -normalize(scene.lightDirection.xyz)), 0.0);
    float lighting = scene.parameters.y > 0.5 ? 1.0 : scene.parameters.x + (1.0 - scene.parameters.x) * diffuse;
    outColor = vec4(base.rgb * lighting, 1.0);
#ifdef AEG_TAA
    float currentNdcDepth = currentClip.z / currentClip.w;
    float currentDepth = scene.temporalInfo.z > 0.5 ? currentClip.w
        : (scene.clipConvention.y > 0.5 ? 1.0 - currentNdcDepth : currentNdcDepth);
    outMotionDepth = vec4(0.0, 0.0, -1.0, currentDepth);
    if (previousClip.w > 0.0) {
        // Positive viewport height maps NDC y=-1 to UV y=0 in both frames.
        vec2 currentUV = currentClip.xy / currentClip.w * 0.5 + 0.5;
        vec2 previousUV = previousClip.xy / previousClip.w * 0.5 + 0.5;
        float previousNdcDepth = previousClip.z / previousClip.w;
        float previousDepth = scene.temporalInfo.w > 0.5 ? previousClip.w
            : (scene.clipConvention.w > 0.5 ? 1.0 - previousNdcDepth : previousNdcDepth);
        outMotionDepth = vec4(currentUV - previousUV, previousDepth, currentDepth);
    }
#endif
}
