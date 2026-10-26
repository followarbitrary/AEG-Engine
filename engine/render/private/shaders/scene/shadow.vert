#version 450
#extension GL_GOOGLE_include_directive : require
#include "scene_uniform.glsl"
layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUV;
layout(location = 0) out vec2 uv;
void main() {
    gl_Position = scene.lightViewProjection * scene.model * vec4(inPosition, 1.0);
    uv = inUV;
}
