#version 450
layout(location = 0) in vec3 normal;
layout(location = 1) in vec2 uv;
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
#ifdef AEG_TAA
    mat4 previousViewProjection;
    mat4 previousModel;
    mat4 currentViewProjection;
    vec4 temporalInfo;
#endif
}
scene;
layout(set = 1, binding = 0) uniform sampler2D baseTexture;
layout(push_constant) uniform Material {
    vec4 baseColor;
    float alphaCutoff;
    uint alphaMasked;
}
material;
void main() {
    vec4 base = texture(baseTexture, uv) * material.baseColor;
    if (material.alphaMasked != 0 && base.a < material.alphaCutoff) discard;
    vec3 n = normalize(normal);
    if (!gl_FrontFacing) n = -n;
    float diffuse = max(dot(n, -normalize(scene.lightDirection.xyz)), 0.0);
    float lighting = scene.parameters.y > 0.5 ? 1.0 : scene.parameters.x + (1.0 - scene.parameters.x) * diffuse;
    outColor = vec4(base.rgb * lighting, 1.0);
#ifdef AEG_TAA
    float currentDepth = scene.temporalInfo.z > 0.5 ? currentClip.w : currentClip.z / currentClip.w;
    outMotionDepth = vec4(0.0, 0.0, -1.0, currentDepth);
    if (previousClip.w > 0.0) {
        // Positive viewport height maps NDC y=-1 to UV y=0 in both frames.
        vec2 currentUV = currentClip.xy / currentClip.w * 0.5 + 0.5;
        vec2 previousUV = previousClip.xy / previousClip.w * 0.5 + 0.5;
        float previousDepth = scene.temporalInfo.w > 0.5 ? previousClip.w : previousClip.z / previousClip.w;
        outMotionDepth = vec4(currentUV - previousUV, previousDepth, currentDepth);
    }
#endif
}
