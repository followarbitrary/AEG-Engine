#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(push_constant) uniform FxaaParameters {
    layout(offset = 0) vec2 inverseResolution;
} fxaa;

// Compact spatial FXAA following the edge, endpoint and subpixel tests described in
// https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf
// Adaptation: gamma-2 luma guides filtering, but all RGB sampling/blending stays linear.
// Endpoint search is limited to 12 texels and uses one bilinear tap per endpoint.
vec3 sampleColor(vec2 position) {
    return textureLod(sceneColor, position, 0.0).rgb;
}

float perceptualLuma(vec3 linearColor) {
    return dot(sqrt(max(linearColor, vec3(0.0))), vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 texel = fxaa.inverseResolution;
    vec3 center = sampleColor(uv);
    vec3 north = sampleColor(uv - vec2(0.0, texel.y));
    vec3 south = sampleColor(uv + vec2(0.0, texel.y));
    vec3 west = sampleColor(uv - vec2(texel.x, 0.0));
    vec3 east = sampleColor(uv + vec2(texel.x, 0.0));
    float lumaM = perceptualLuma(center);
    float lumaN = perceptualLuma(north);
    float lumaS = perceptualLuma(south);
    float lumaW = perceptualLuma(west);
    float lumaE = perceptualLuma(east);
    float rangeMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float rangeMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float contrast = rangeMax - rangeMin;
    if (contrast < max(1.0 / 32.0, rangeMax / 8.0)) {
        // Flat interiors retain their exact original color.
        outColor = vec4(center, 1.0);
        return;
    }

    vec3 northWest = sampleColor(uv + vec2(-texel.x, -texel.y));
    vec3 northEast = sampleColor(uv + vec2(texel.x, -texel.y));
    vec3 southWest = sampleColor(uv + vec2(-texel.x, texel.y));
    vec3 southEast = sampleColor(uv + vec2(texel.x, texel.y));
    float lumaNW = perceptualLuma(northWest);
    float lumaNE = perceptualLuma(northEast);
    float lumaSW = perceptualLuma(southWest);
    float lumaSE = perceptualLuma(southEast);

    float horizontal = abs(lumaNW - 2.0 * lumaW + lumaSW)
                     + 2.0 * abs(lumaN - 2.0 * lumaM + lumaS)
                     + abs(lumaNE - 2.0 * lumaE + lumaSE);
    float vertical = abs(lumaNW - 2.0 * lumaN + lumaNE)
                   + 2.0 * abs(lumaW - 2.0 * lumaM + lumaE)
                   + abs(lumaSW - 2.0 * lumaS + lumaSE);
    bool horizontalEdge = horizontal >= vertical;
    float negativeLuma = horizontalEdge ? lumaN : lumaW;
    float positiveLuma = horizontalEdge ? lumaS : lumaE;
    float negativeGradient = abs(negativeLuma - lumaM);
    float positiveGradient = abs(positiveLuma - lumaM);
    bool useNegative = negativeGradient >= positiveGradient;
    float gradient = max(negativeGradient, positiveGradient);
    float edgeLuma = 0.5 * (lumaM + (useNegative ? negativeLuma : positiveLuma));
    vec2 normalStep = horizontalEdge ? vec2(0.0, texel.y) : vec2(texel.x, 0.0);
    if (useNegative) normalStep = -normalStep;
    vec2 tangentStep = horizontalEdge ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    vec2 edgeCenter = uv + 0.5 * normalStep;

    float negativeDistance = 1.0;
    float positiveDistance = 1.0;
    float negativeDelta = 0.0;
    float positiveDelta = 0.0;
    bool negativeFound = false;
    bool positiveFound = false;
    for (int stepIndex = 1; stepIndex <= 12; ++stepIndex) {
        float distanceAlongEdge = float(stepIndex);
        if (!negativeFound) {
            negativeDistance = distanceAlongEdge;
            negativeDelta = perceptualLuma(sampleColor(edgeCenter - tangentStep * distanceAlongEdge)) - edgeLuma;
            negativeFound = abs(negativeDelta) >= gradient * 0.25;
        }
        if (!positiveFound) {
            positiveDistance = distanceAlongEdge;
            positiveDelta = perceptualLuma(sampleColor(edgeCenter + tangentStep * distanceAlongEdge)) - edgeLuma;
            positiveFound = abs(positiveDelta) >= gradient * 0.25;
        }
        if (negativeFound && positiveFound) break;
    }

    bool nearerNegative = negativeDistance < positiveDistance;
    bool endpointFound = nearerNegative ? negativeFound : positiveFound;
    float endpointDelta = nearerNegative ? negativeDelta : positiveDelta;
    float nearerDistance = min(negativeDistance, positiveDistance);
    float edgeOffset = 0.0;
    // Reject a same-side endpoint or an exhausted search to avoid shifting across an unrelated edge.
    if (endpointFound && ((endpointDelta < 0.0) != (lumaM < edgeLuma))) {
        edgeOffset = 0.5 - nearerDistance / (negativeDistance + positiveDistance);
    }
    vec3 edgeFiltered = sampleColor(uv + normalStep * edgeOffset);

    float neighborLuma = 0.25 * (lumaN + lumaS + lumaW + lumaE);
    float subpixelBlend = clamp((abs(neighborLuma - lumaM) / contrast - 0.25) / 0.75, 0.0, 0.75);
    vec3 neighborhood = (center + north + south + west + east
                       + northWest + northEast + southWest + southEast) / 9.0;
    outColor = vec4(mix(edgeFiltered, neighborhood, subpixelBlend), 1.0);
}
