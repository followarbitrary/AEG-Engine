#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_shader_draw_parameters : require
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
#include "scene_uniform.glsl"
layout(location = 0) out vec3 normal;
layout(location = 1) out vec2 uv;
layout(location = 4) out vec4 tangentFrame;
layout(location = 5) out vec3 worldPosition;
layout(location = 6) flat out uint materialIndex;
void main() {
    vec4 world = scene.model * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    materialIndex = gl_BaseInstanceARB;
    gl_Position = scene.viewProjection * world;
    normal = transpose(inverse(mat3(scene.model))) * inNormal;
    tangentFrame =
        vec4(mat3(scene.model) * inTangent.xyz, inTangent.w * (determinant(mat3(scene.model)) < 0.0 ? -1.0 : 1.0));
    uv = inUV;
}
