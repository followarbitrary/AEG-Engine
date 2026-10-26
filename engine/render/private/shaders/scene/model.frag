#version 450
#extension GL_GOOGLE_include_directive : require
layout(location = 0) in vec3 normal;
layout(location = 1) in vec2 uv;
layout(location = 4) in vec4 tangentFrame;
layout(location = 5) in vec3 worldPosition;
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
    vec4 cameraPosition;
    vec4 cameraTowardViewer;
    vec4 lightRadiance;
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
    uint alphaMode;
    float normalScale;
    uint hasNormalMap;
    vec4 pbrFactors;
}
material;
#include "normal_mapping.glsl"
#include "pbr_lighting.glsl"
void main() {
    vec4 base = texture(baseTexture, uv) * material.baseColor;
    if (material.alphaMode == 1 && base.a < material.alphaCutoff) discard;
    float opacity = material.alphaMode == 2 ? clamp(base.a, 0.0, 1.0) : 1.0;
    if (material.alphaMode == 2 && opacity <= 0.0) discard;
    vec3 n = safePbrDirection(normal, vec3(0.0, 0.0, 1.0));
    if (material.hasNormalMap != 0 && scene.parameters.z > 0.5 && scene.parameters.w > 0.0 && scene.parameters.y < 0.5)
        n = applyNormalMap(n, tangentFrame, texture(normalTexture, uv).rgb, material.normalScale, scene.parameters.w);
    // A reflected model reverses winding while inverse-transpose normals retain
    // their authored outward direction. Compensate before two-sided shading.
    bool mirroredModel = determinant(mat3(scene.model)) < 0.0;
    if (gl_FrontFacing == mirroredModel) n = -n;
    vec3 color = base.rgb;
    if (scene.parameters.y < 0.5) {
        vec3 v = scene.cameraPosition.w > 0.5
                     ? safePbrDirection(scene.cameraPosition.xyz - worldPosition, scene.cameraTowardViewer.xyz)
                     : scene.cameraTowardViewer.xyz;
        color = pbrLighting(base.rgb, material.pbrFactors.x, material.pbrFactors.y, n, -scene.lightDirection.xyz, v,
                            scene.lightRadiance.rgb, scene.parameters.x);
    }
    outColor = vec4(color, opacity);
#ifdef AEG_TAA
    if (material.alphaMode == 2) {
        // Blended surfaces do not own a single opaque depth or motion vector.
        // This attachment is unblended; mark the covered pixels as reactive.
        outMotionDepth = vec4(0.0, 0.0, -1.0, -2.0);
        return;
    }
    float currentNdcDepth = currentClip.z / currentClip.w;
    float currentDepth = scene.temporalInfo.z > 0.5
                             ? currentClip.w
                             : (scene.clipConvention.y > 0.5 ? 1.0 - currentNdcDepth : currentNdcDepth);
    outMotionDepth = vec4(0.0, 0.0, -1.0, currentDepth);
    if (previousClip.w > 0.0) {
        // Positive viewport height maps NDC y=-1 to UV y=0 in both frames.
        vec2 currentUV = currentClip.xy / currentClip.w * 0.5 + 0.5;
        vec2 previousUV = previousClip.xy / previousClip.w * 0.5 + 0.5;
        float previousNdcDepth = previousClip.z / previousClip.w;
        float previousDepth = scene.temporalInfo.w > 0.5
                                  ? previousClip.w
                                  : (scene.clipConvention.w > 0.5 ? 1.0 - previousNdcDepth : previousNdcDepth);
        outMotionDepth = vec4(currentUV - previousUV, previousDepth, currentDepth);
    }
#endif
}
