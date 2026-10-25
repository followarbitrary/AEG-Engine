#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(set = 0, binding = 0) uniform Scene {
    mat4 viewProjection;
    mat4 model;
    vec4 lightDirection;
    vec4 parameters;
}
scene;
layout(location = 0) out vec3 normal;
layout(location = 1) out vec2 uv;
void main() {
    gl_Position = scene.viewProjection * scene.model * vec4(inPosition, 1.0);
    normal = transpose(inverse(mat3(scene.model))) * inNormal;
    uv = inUV;
}
