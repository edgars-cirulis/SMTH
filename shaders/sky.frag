#version 450

layout(push_constant) uniform PC {
    vec3 camForward;
    float tanHalfFov;
    vec3 camRight;
    float aspect;
    vec3 camUp;
    float time;
    vec3 sunDir;
    float _pad;
} pc;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;


float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += a * noise(p);
        p = p * 2.02 + vec2(17.0, 13.0);
        a *= 0.5;
    }
    return v;
}

void main() {

    vec2 uv = vec2(vNdc.x, -vNdc.y);
    vec3 dir = normalize(
        pc.camForward +
        uv.x * pc.camRight * (pc.tanHalfFov * pc.aspect) +
        uv.y * pc.camUp * pc.tanHalfFov
    );


    float h = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 zenith = vec3(0.05, 0.10, 0.22);
    vec3 horizon = vec3(0.55, 0.65, 0.75);
    vec3 sky = mix(horizon, zenith, pow(h, 1.7));


    vec3 sdir = normalize(pc.sunDir);
    float sunDot = clamp(dot(dir, sdir), 0.0, 1.0);
    float sunCore = smoothstep(0.9992, 1.0, sunDot);
    float sunHalo = smoothstep(0.98, 1.0, sunDot) * 0.35;
    vec3 sunCol = vec3(1.0, 0.95, 0.85) * (sunCore * 6.0 + sunHalo);


    sky += vec3(0.25, 0.22, 0.18) * pow(sunDot, 32.0);



    float cloudMask = smoothstep(0.0, 0.35, dir.y) * smoothstep(0.95, 0.55, dir.y);
    vec2 p = dir.xz / max(0.15, dir.y + 0.25);
    p *= 0.45;
    p += vec2(pc.time * 0.01, pc.time * 0.006);
    float n = fbm(p);
    float clouds = smoothstep(0.55, 0.78, n) * cloudMask;


    float light = 0.55 + 0.45 * pow(clamp(dot(normalize(vec3(dir.x, 0.35, dir.z)), sdir), 0.0, 1.0), 8.0);
    vec3 cloudCol = mix(vec3(0.85, 0.88, 0.92), vec3(1.0, 0.98, 0.92), light);

    vec3 col = sky;
    col = mix(col, cloudCol, clouds * 0.85);
    col += sunCol;


    col = col / (col + vec3(1.0));

    outColor = vec4(col, 1.0);
}
