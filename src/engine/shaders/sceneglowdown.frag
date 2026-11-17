#version 440
// Its glow: what shines, halved and spread again, each step wider (the port of Model3D.cpp's kGlowDown)
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 o;
layout(std140, binding = 1) uniform Glow { vec4 u_texel4; vec4 u_weight4; };
#define u_texel u_texel4.xy
#define u_weight u_weight4.x
layout(binding = 2) uniform sampler2D u_from;
void main() {
    vec2 uv = v_ndc * 0.5 + 0.5;
    vec3 c = texture(u_from, uv).rgb * 4.0;
    for (int y = -1; y <= 1; y += 2)
        for (int x = -1; x <= 1; x += 2) c += texture(u_from, uv + vec2(x, y) * u_texel).rgb;
    o = vec4(c / 8.0, 1.0);
}
