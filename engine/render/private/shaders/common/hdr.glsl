#ifndef AEG_HDR_GLSL
#define AEG_HDR_GLSL

// Bound working radiance before half-float storage; do not clip HDR to display white.
float finiteHdrRadiance(float value) {
    if (isnan(value) || value <= 0.0) return 0.0;
    // Includes positive infinity without relying on clamp's handling of NaNs.
    return min(value, 65504.0);
}

vec3 finiteHdrRadiance(vec3 value) {
    return vec3(finiteHdrRadiance(value.r), finiteHdrRadiance(value.g), finiteHdrRadiance(value.b));
}

#endif
