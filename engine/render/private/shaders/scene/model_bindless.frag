#version 450
#extension GL_GOOGLE_include_directive : require
#include "../common/hdr.glsl"
layout(location = 0) in vec3 normal;
layout(location = 1) in vec2 uv;
layout(location = 4) in vec4 tangentFrame;
layout(location = 5) in vec3 worldPosition;
layout(location = 0) out vec4 outColor;
#include "scene_uniform.glsl"
layout(set = 1, binding = 0) uniform sampler2D baseTextures[256];
layout(set = 1, binding = 1) uniform sampler2D normalTextures[256];
layout(push_constant) uniform Material {
    vec4 baseColor;
    float alphaCutoff;
    uint alphaMode;
    float normalScale;
    uint hasNormalMap;
    vec4 pbrFactors;
    uint baseTextureIndex;
    uint normalTextureIndex;
    uint padding0;
    uint padding1;
}
material;
#include "normal_mapping.glsl"
#include "pbr_lighting.glsl"
#include "ibl_lighting.glsl"
#include "punctual_lighting.glsl"
#include "shadow_sampling.glsl"
void main() {
    ShadowGradients shadowGradients = shadowDepthGradients(worldPosition);
    vec4 base = texture(baseTextures[material.baseTextureIndex], uv) * material.baseColor;
    if (material.alphaMode == 1u && base.a < material.alphaCutoff) discard;
    float opacity = material.alphaMode == 2u ? clamp(base.a, 0.0, 1.0) : 1.0;
    if (material.alphaMode == 2u && opacity <= 0.0) discard;
    vec3 n = safePbrDirection(normal, vec3(0.0, 0.0, 1.0)), geometricNormal = n;
    if (material.hasNormalMap != 0u && scene.parameters.z > 0.5 && scene.parameters.w > 0.0 && scene.parameters.y < 0.5)
        n = applyNormalMap(n, tangentFrame, texture(normalTextures[material.normalTextureIndex], uv).rgb,
                           material.normalScale, scene.parameters.w);
    bool mirroredModel = determinant(mat3(scene.model)) < 0.0;
    if (gl_FrontFacing == mirroredModel) { n = -n; geometricNormal = -geometricNormal; }
    vec3 color = base.rgb;
    if (scene.parameters.y < 0.5) {
        vec3 v = scene.cameraPosition.w > 0.5
                     ? safePbrDirection(scene.cameraPosition.xyz - worldPosition, scene.cameraTowardViewer.xyz)
                     : scene.cameraTowardViewer.xyz;
        color = scene.parameters.x * clamp(base.rgb, vec3(0.0), vec3(1.0)) * (1.0 - material.pbrFactors.x);
        if (scene.environmentInfo.x > 0.5)
            color = iblLighting(base.rgb, material.pbrFactors.x, material.pbrFactors.y, n, v);
        for (int i = 0; i < int(scene.lightInfo.x); ++i) {
            vec3 towardLight, radiance;
            if (!punctualLight(scene.lights[i], worldPosition, towardLight, radiance)) continue;
            float visibility = shadowVisibility(worldPosition, geometricNormal, towardLight, uint(i), shadowGradients);
            color += pbrDirectLighting(base.rgb, material.pbrFactors.x, material.pbrFactors.y, n, towardLight, v,
                                       radiance) * visibility;
        }
    }
    outColor = vec4(finiteHdrRadiance(color), opacity);
}
