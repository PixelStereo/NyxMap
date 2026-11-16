#version 440
// The same, back up: each level added to the one above it, as wide as its size says (kGlowUp)
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 o;
layout(std140, binding = 1) uniform Glow { vec4 u_texel4; vec4 u_weight4; };
#define u_texel u_texel4.xy
#define u_weight u_weight4.x
layout(binding = 2) uniform sampler2D u_from;
void main() {
    vec2 uv = v_ndc * 0.5 + 0.5;
    vec3 c = vec3(0.0);
    for (int y = -1; y <= 1; y += 2)
        for (int x = -1; x <= 1; x += 2) {
            c += texture(u_from, uv + vec2(x, y) * u_texel).rgb * 2.0;
            c += texture(u_from, uv + vec2(x * 2, 0) * u_texel).rgb * 0.5;
            c += texture(u_from, uv + vec2(0, y * 2) * u_texel).rgb * 0.5;
        }
    o = vec4(c / 12.0 * u_weight, 1.0);
}
