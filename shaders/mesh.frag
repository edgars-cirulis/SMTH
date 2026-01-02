#version 450

#include "shared.glsl"

layout(location = 0) in vec3 vNrm;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec3 vPosW;
layout(location = 3) in vec4 vTangent;



layout(location = 0) out vec4 outColor;



const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NoH = max(dot(N, H), 0.0);
    float NoH2 = NoH * NoH;
    float denom = (NoH2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-6);
}

float GeometrySchlickGGX(float NoV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0; 
    return NoV / max(NoV * (1.0 - k) + k, 1e-6);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NoV = max(dot(N, V), 0.0);
    float NoL = max(dot(N, L), 0.0);
    float ggxV = GeometrySchlickGGX(NoV, roughness);
    float ggxL = GeometrySchlickGGX(NoL, roughness);
    return ggxV * ggxL;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 acesTonemap(vec3 x) {
    
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    
    vec3 N = normalize(vNrm);
    vec3 T = normalize(vTangent.xyz);
    
    T = normalize(T - N * dot(N, T));
    vec3 B = cross(N, T) * vTangent.w;
    mat3 TBN = mat3(T, B, N);

    
    vec3 baseTex = texture(tBaseColor, vUv).rgb;
    vec3 albedo = (uMaterial.baseColorFactor.rgb * baseTex);

    vec3 nTex = texture(tNormal, vUv).xyz * 2.0 - 1.0;
    vec3 Nw = normalize(TBN * nTex);

    vec3 mr = texture(tMetalRough, vUv).rgb;
    float roughTex = mr.g;
    float metalTex = mr.b;

    float metallic = clamp(uMaterial.metallicRoughnessFactor.x * metalTex, 0.0, 1.0);
    float roughness = clamp(uMaterial.metallicRoughnessFactor.y * roughTex, 0.04, 1.0);

    vec3 V = normalize(uCamera.camPos - vPosW);
    vec3 L = normalize(uLight.lightDir);
    vec3 H = normalize(V + L);

    float NoL = max(dot(Nw, L), 0.0);
    float NoV = max(dot(Nw, V), 0.0);
    float VoH = max(dot(V, H), 0.0);

    vec3 radiance = uLight.lightColor * uLight.lightIntensity;

    
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F  = FresnelSchlick(VoH, F0);
    float D = DistributionGGX(Nw, H, roughness);
    float G = GeometrySmith(Nw, V, L, roughness);

    vec3 numerator = D * G * F;
    float denom = max(4.0 * NoV * NoL, 1e-6);
    vec3 specular = numerator / denom;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 diffuse = (kD * albedo) / PI;
    vec3 col = (diffuse + specular) * radiance * NoL;

    float hemi = clamp(Nw.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyAmb = mix(vec3(0.02, 0.02, 0.025), vec3(0.12, 0.15, 0.20), pow(hemi, 1.4));
    col += (albedo / PI) * skyAmb;
    col += F0 * 0.02;

    col *= max(uLight.exposure, 0.0001);
    col = acesTonemap(col);
    col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(col, 1.0);
}
