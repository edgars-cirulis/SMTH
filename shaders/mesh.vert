#version 450

#include "shared.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inTangent;



layout(location = 0) out vec3 vNrm;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec3 vPosW;
layout(location = 3) out vec4 vTangent;

void main() {
    uint transformIndex = uint(gl_InstanceIndex);
    mat4 model = uTransforms.models[transformIndex];
    vec4 posW4 = model * vec4(inPos, 1.0);
    vPosW = posW4.xyz;

    mat3 nrmMat = mat3(transpose(inverse(model)));
    vNrm = normalize(nrmMat * inNrm);

    vUv = inUv;
    vTangent = inTangent;
    gl_Position = uCamera.proj * uCamera.view * posW4;
}
