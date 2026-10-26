// RGB tangent-space normals use linear data, with scale on X/Y only.
// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materialnormaltexture
vec3 applyNormalMap(vec3 n, vec4 tangent, vec3 encodedNormal, float scale, float strength) {
    if (abs(tangent.w) < 0.5 || scale == 0.0 || strength == 0.0 || any(isnan(tangent)) || any(isinf(tangent))) return n;
    float magnitude = max(max(abs(tangent.x), abs(tangent.y)), abs(tangent.z));
    if (magnitude < 1e-12) return n;
    vec3 t = tangent.xyz / magnitude;
    t -= n * dot(n, t);
    float tangentLengthSquared = dot(t, t);
    if (tangentLengthSquared < 1e-12) return n;
    t *= inversesqrt(tangentLengthSquared);
    vec3 b = cross(n, t) * (tangent.w < 0.0 ? -1.0 : 1.0);
    vec3 mapped = encodedNormal * 2.0 - 1.0;
    // Scale before normalization without overflowing for large finite glTF scales.
    if (abs(scale) > 1.0)
        mapped = vec3(mapped.xy * sign(scale) * strength, mapped.z / abs(scale));
    else
        mapped.xy *= scale * strength;
    float largest = max(max(abs(mapped.x), abs(mapped.y)), abs(mapped.z));
    if (largest < 1e-12) return n;
    mapped = normalize(mapped / largest);
    return normalize(t * mapped.x + b * mapped.y + n * mapped.z);
}
