// Full layout is shared by ordinary, temporal and shadow shader variants.
struct GpuLight {
    vec4 positionRange;
    vec4 directionType;
    vec4 radiance;
    vec4 cone;
};
layout(set = 0, binding = 0) uniform Scene {
    mat4 viewProjection;
    mat4 model;
    vec4 parameters;
    vec4 clipConvention;
    vec4 cameraPosition;
    vec4 cameraTowardViewer;
    mat4 previousViewProjection;
    mat4 previousModel;
    mat4 currentViewProjection;
    vec4 temporalInfo;
    vec4 lightInfo;
    GpuLight lights[8];
    mat4 lightViewProjection;
    vec4 shadowInfo;
    vec4 shadowBias;
}
scene;
