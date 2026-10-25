#version 450

layout(location = 0) out vec2 uv;

void main() {
    uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    // A positive Vulkan viewport height maps NDC y=-1 to the top row, UV y=0.
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
