// Returns surface-to-light direction and finite incident radiance.
bool punctualLight(GpuLight light, vec3 position, out vec3 towardLight, out vec3 radiance) {
    towardLight = -light.directionType.xyz;
    radiance = vec3(0.0);
    if (light.radiance.w < 0.5) return false;
    if (light.directionType.w < 0.5) {
        radiance = light.radiance.rgb;
        return true;
    }
    vec3 delta = light.positionRange.xyz - position;
    if (any(isnan(delta)) || any(isinf(delta))) return false;
    float largest = max(max(abs(delta.x), abs(delta.y)), abs(delta.z));
    // At the source the direction is undefined; a punctual light contributes zero.
    if (largest <= 0.0) return false;
    vec3 scaled = delta / largest;
    float scaledLength = length(scaled);
    towardLight = scaled / scaledLength;
    float distanceToLight = largest * scaledLength;
    float range = light.positionRange.w;
    if (range <= 0.0 || distanceToLight >= range) return false;
    float ratio = distanceToLight / range;
    float ratioSquared = ratio * ratio;
    float cutoff = max(1.0 - ratioSquared * ratioSquared, 0.0);
    float weight = cutoff * cutoff;
    if (light.directionType.w > 1.5) {
        float cosine = clamp(dot(-towardLight, light.directionType.xyz), -1.0, 1.0);
        float inner = light.cone.x, outer = light.cone.y;
        float angular = inner > outer ? smoothstep(outer, inner, cosine) : (cosine >= outer ? 1.0 : 0.0);
        weight *= angular;
    }
    if (weight <= 0.0) return false;
    // Divide in sequence: squaring a large finite distance overflows, while
    // squaring its reciprocal can underflow before a bright source compensates.
    // The floor regularizes near-source radiance without changing its direction.
    float minimumDistance = max(distanceToLight, 0.01);
    precise vec3 incident = light.radiance.rgb * weight;
    incident /= minimumDistance;
    incident /= minimumDistance;
    radiance = min(incident, vec3(65504.0));
    return true;
}
