#ifndef AEG_IBL_SHARED_GLSL
#define AEG_IBL_SHARED_GLSL

const float iblPi = 3.14159265358979323846;

// Shared with the CPU bake dispatch: exactly sixteen bytes.
layout(push_constant) uniform IblPush {
    uint size;
    uint sampleCount;
    float roughness;
    uint sourceSize;
}
ibl;

vec3 iblCubeDirection(uint face, vec2 pixel, uint size) {
    vec2 st = 2.0 * pixel / float(size) - 1.0;
    if (face == 0u) return normalize(vec3(1.0, -st.y, -st.x));
    if (face == 1u) return normalize(vec3(-1.0, -st.y, st.x));
    if (face == 2u) return normalize(vec3(st.x, 1.0, st.y));
    if (face == 3u) return normalize(vec3(st.x, -1.0, -st.y));
    if (face == 4u) return normalize(vec3(st.x, -st.y, 1.0));
    return normalize(vec3(-st.x, -st.y, -1.0));
}

vec2 iblEquirectUv(vec3 direction) {
    return vec2(atan(direction.z, direction.x) / (2.0 * iblPi) + 0.5, acos(clamp(direction.y, -1.0, 1.0)) / iblPi);
}

vec2 iblHammersley(uint index, uint count) {
    return vec2(float(index) / float(count), float(bitfieldReverse(index)) * 2.3283064365386963e-10);
}

mat3 iblTangentFrame(vec3 n) {
    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, n));
    return mat3(tangent, cross(n, tangent), n);
}

vec3 iblCosineHemisphere(vec2 xi) {
    float phi = 2.0 * iblPi * xi.x;
    float radius = sqrt(xi.y);
    return vec3(radius * cos(phi), radius * sin(phi), sqrt(1.0 - xi.y));
}

// Perceptual roughness is squared once to obtain GGX alpha.
vec3 iblGgxHalfVector(vec2 xi, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float phi = 2.0 * iblPi * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alphaSquared - 1.0) * xi.y));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float iblGgxDistribution(float noH, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    // Avoid cancellation at the peak of a narrow lobe, as in direct PBR.
    float denominator = alphaSquared * noH * noH + (1.0 - noH * noH);
    return alphaSquared / (iblPi * denominator * denominator);
}

float iblCorrelatedSmithVisibility(float noL, float noV, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float smith = noL * sqrt(noV * noV * (1.0 - alphaSquared) + alphaSquared) +
                  noV * sqrt(noL * noL * (1.0 - alphaSquared) + alphaSquared);
    return 0.5 / max(smith, 1e-8);
}

#endif
