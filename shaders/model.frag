#version 450
layout(location = 0) in vec3 normal;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform Scene {
    mat4 viewProjection;
    mat4 model;
    vec4 lightDirection;
    vec4 parameters;
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
}
