#ifndef SHARED_GLSL
#define SHARED_GLSL

#define SET_FRAME 0
#define SET_MATERIAL 1

#define BIND_CAMERA 0
#define BIND_LIGHT 1
#define BIND_TRANSFORMS 2

#define BIND_BASE_COLOR 0
#define BIND_NORMAL 1
#define BIND_METAL_ROUGH 2
#define BIND_MATERIAL 3

layout(std140, set = SET_FRAME, binding = BIND_CAMERA) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    vec3 camPos;
    float _pad0;
} uCamera;

layout(std140, set = SET_FRAME, binding = BIND_LIGHT) uniform LightUBO {
    vec3 lightDir;
    float lightIntensity;
    vec3 lightColor;
    float exposure;
} uLight;

layout(std430, set = SET_FRAME, binding = BIND_TRANSFORMS) readonly buffer Transforms {
    mat4 models[];
} uTransforms;

layout(std140, set = SET_MATERIAL, binding = BIND_MATERIAL) uniform MaterialUBO {
    vec4 baseColorFactor;
    vec2 metallicRoughnessFactor;
    vec2 _pad0;
} uMaterial;

layout(set = SET_MATERIAL, binding = BIND_BASE_COLOR) uniform sampler2D tBaseColor;
layout(set = SET_MATERIAL, binding = BIND_NORMAL) uniform sampler2D tNormal;
layout(set = SET_MATERIAL, binding = BIND_METAL_ROUGH) uniform sampler2D tMetalRough;

#endif
