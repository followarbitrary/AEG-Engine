#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
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
layout(location = 0) out vec3 normal;
layout(location = 1) out vec2 uv;
layout(location = 4) out vec4 tangentFrame;
#ifdef AEG_TAA
layout(location = 2) out vec4 currentClip;
layout(location = 3) out vec4 previousClip;
#endif
void main() {
    gl_Position = scene.viewProjection * scene.model * vec4(inPosition, 1.0);
    normal = transpose(inverse(mat3(scene.model))) * inNormal;
    // Normals and tangents transform differently under nonuniform scaling.
    tangentFrame = vec4(mat3(scene.model) * inTangent.xyz,
        inTangent.w * (determinant(mat3(scene.model)) < 0.0 ? -1.0 : 1.0));
    uv = inUV;
#ifdef AEG_TAA
    // Rasterization is jittered, while motion tracks the unjittered surface.
    currentClip = scene.currentViewProjection * scene.model * vec4(inPosition, 1.0);
    previousClip = scene.previousViewProjection * scene.previousModel * vec4(inPosition, 1.0);
#endif
}
