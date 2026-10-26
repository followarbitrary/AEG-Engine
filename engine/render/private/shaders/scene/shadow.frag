#version 450
layout(location = 0) in vec2 uv;
layout(set = 1, binding = 0) uniform sampler2D baseTexture;
layout(push_constant) uniform Material {
    vec4 baseColor;
    float alphaCutoff;
    uint alphaMode;
    float normalScale;
    uint hasNormalMap;
    vec4 pbrFactors;
}
material;
void main() {
    if (material.alphaMode == 1 && texture(baseTexture, uv).a * material.baseColor.a < material.alphaCutoff) discard;
}
