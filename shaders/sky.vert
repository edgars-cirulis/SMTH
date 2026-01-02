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

layout(location = 0) out vec2 vNdc;

void main() {
    vec2 pos;
    if (gl_VertexIndex == 0) {
        pos = vec2(-1.0, -1.0);
    } else if (gl_VertexIndex == 1) {
        pos = vec2(3.0, -1.0);
    } else {
        pos = vec2(-1.0, 3.0);
    }

    vNdc = pos;
    gl_Position = vec4(pos, 0.0, 1.0);
}
