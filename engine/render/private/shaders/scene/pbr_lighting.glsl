// Cook-Torrance: GGX NDF, height-correlated Smith visibility and Schlick Fresnel.
// Perceptual roughness is squared once to obtain the microfacet alpha parameter.
const float pbrPi = 3.14159265358979323846;
vec3 safePbrDirection(vec3 direction, vec3 fallback) {
    float largest = max(max(abs(direction.x), abs(direction.y)), abs(direction.z));
    if (largest <= 0.0 || any(isnan(direction)) || any(isinf(direction))) return fallback;
    return normalize(direction / largest);
}
vec3 pbrDirectLighting(vec3 base, float metallic, float perceptualRoughness, vec3 n, vec3 l, vec3 v, vec3 radiance) {
    // Reflectance stays physical even for legacy materials with out-of-range color factors.
    // The unlit path preserves the original color-factor contract.
    base = clamp(base, vec3(0.0), vec3(1.0));
    float noL = clamp(dot(n, l), 0.0, 1.0);
    float noV = clamp(dot(n, v), 0.0, 1.0);
    if (noL <= 0.0 || noV <= 0.0) return vec3(0.0);
    vec3 sum = l + v;
    if (dot(sum, sum) < 1e-12) return vec3(0.0);
    vec3 h = normalize(sum);
    float noH = clamp(dot(n, h), 0.0, 1.0);
    float voH = clamp(dot(v, h), 0.0, 1.0);
    float roughness = clamp(perceptualRoughness, 0.045, 1.0);
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    // This sum avoids cancellation at the narrow lobe's peak.
    float denominator = alphaSquared * noH * noH + (1.0 - noH * noH);
    float distribution = alphaSquared / (pbrPi * denominator * denominator);
    float smith = noL * sqrt(noV * noV * (1.0 - alphaSquared) + alphaSquared) +
                  noV * sqrt(noL * noL * (1.0 - alphaSquared) + alphaSquared);
    // Only the numerically extreme grazing limit reaches this floor.
    float visibility = 0.5 / max(smith, 1e-8);
    vec3 f0 = mix(vec3(0.04), base, metallic);
    float oneMinus = 1.0 - voH;
    float fifth = oneMinus * oneMinus * oneMinus * oneMinus * oneMinus;
    vec3 fresnel = f0 + (vec3(1.0) - f0) * fifth;
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * base / pbrPi;
    return (diffuse + distribution * visibility * fresnel) * radiance * noL;
}
