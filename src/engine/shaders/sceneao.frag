#version 440
// Where surfaces meet (the port of Model3D.cpp's kAoFragment): from what is seen, how much of the half-sphere over each
// point its neighbours hide
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 o;
layout(std140, binding = 1) uniform Ao { mat4 u_invProj; mat4 u_proj; vec4 u_params; ivec4 u_ints; }; // radius, amount; samples
#define u_radius u_params.x
#define u_amount u_params.y
#define u_samples u_ints.x
layout(binding = 2) uniform sampler2D u_seen;
vec3 at(vec2 uv) {
    vec4 p = u_invProj * vec4(uv * 2.0 - 1.0, texture(u_seen, uv).r * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
void main() {
    vec2 uv = v_ndc * 0.5 + 0.5;
    if (texture(u_seen, uv).r >= 1.0) { o = vec4(1.0); return; }
    vec3 P = at(uv);
    vec3 N = normalize(cross(dFdx(P), dFdy(P)));
    if (dot(N, P) > 0.0) N = -N;
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float hidden = 0.0;
    int count = u_samples;
    for (int k = 0; k < count; ++k) {
        float t = (float(k) + 0.5) / float(count), a = (noise + float(k) * 0.61803399) * 6.2831853;
        float rr = sqrt(t), z = sqrt(max(0.0, 1.0 - t));
        vec3 h = vec3(cos(a) * rr, sin(a) * rr, z);
        vec3 tx = normalize(cross(N, abs(N.y) > 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0))), ty = cross(N, tx);
        vec3 s = P + (tx * h.x + ty * h.y + N * h.z) * u_radius * mix(0.2, 1.0, t);
        vec4 q = u_proj * vec4(s, 1.0);
        vec2 suv = q.xy / q.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.y < 0.0 || suv.x > 1.0 || suv.y > 1.0) continue;
        float there = at(suv).z;
        float close = smoothstep(0.0, 1.0, u_radius / max(abs(P.z - there), 1e-4));
        hidden += (there >= s.z + u_radius * 0.03 ? 1.0 : 0.0) * close;
    }
    o = vec4(pow(clamp(1.0 - hidden / float(count), 0.0, 1.0), 1.0 + u_amount * 1.5));
}
