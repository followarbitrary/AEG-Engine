layout(set = 3, binding = 0) uniform samplerCube environmentIrradiance;
layout(set = 3, binding = 1) uniform samplerCube environmentPrefiltered;
layout(set = 3, binding = 2) uniform sampler2D environmentBrdf;

vec3 environmentDirection(vec3 direction) {
    // World direction is transformed by the inverse of the environment's +Y rotation.
    float c = cos(scene.environmentInfo.z), s = sin(scene.environmentInfo.z);
    return vec3(c * direction.x - s * direction.z, direction.y, s * direction.x + c * direction.z);
}

vec3 iblLighting(vec3 base, float metallic, float perceptualRoughness, vec3 n, vec3 v) {
    base = clamp(base, vec3(0.0), vec3(1.0));
    float roughness = clamp(perceptualRoughness, 0.045, 1.0);
    float noV = clamp(dot(n, v), 0.0, 1.0);
    vec3 f0 = mix(vec3(0.04), base, metallic);
    float oneMinus = 1.0 - noV;
    float fifth = oneMinus * oneMinus * oneMinus * oneMinus * oneMinus;
    // The diffuse Fresnel partition is the roughness-aware split-sum approximation.
    vec3 fresnel = f0 + (max(vec3(1.0 - roughness), f0) - f0) * fifth;
    vec3 irradiance = textureLod(environmentIrradiance, environmentDirection(n), 0.0).rgb;
    float roughnessCoordinate = (roughness - 0.045) / 0.955;
    vec3 reflected = environmentDirection(reflect(-v, n));
    vec3 filtered = textureLod(environmentPrefiltered, reflected, roughnessCoordinate * scene.environmentInfo.w).rgb;
    vec2 brdf = textureLod(environmentBrdf, vec2(noV, roughnessCoordinate), 0.0).rg;
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * base * irradiance / pbrPi;
    vec3 specular = filtered * (f0 * brdf.x + brdf.y);
    return (diffuse + specular) * scene.environmentInfo.y;
}
