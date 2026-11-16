#version 440
// Its noise taken away, across no edge of what is seen
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 o;
layout(std140, binding = 1) uniform Blur { vec4 u_texel4; };
#define u_texel u_texel4.xy
layout(binding = 2) uniform sampler2D u_seen;
layout(binding = 3) uniform sampler2D u_ao;
void main() {
    vec2 uv = v_ndc * 0.5 + 0.5;
    float depth = texture(u_seen, uv).r, sum = 0.0, weight = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            vec2 at = uv + vec2(x, y) * u_texel;
            float w = 1.0 / (1e-5 + abs(texture(u_seen, at).r - depth) * 2000.0 + 1e-3);
            sum += texture(u_ao, at).r * w;
            weight += w;
        }
    o = vec4(sum / max(weight, 1e-5));
}
