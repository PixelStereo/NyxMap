#version 440
// A picture through a matte in Picture space (Layer.h): the matte stretched over it, its luma or its alpha
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform Matte { vec4 params; }; // x: the mode (1 + channel + 2 * inverted)
layout(binding = 1) uniform sampler2D tex;
layout(binding = 2) uniform sampler2D matteTex;
void main() {
    vec4 m = texture(matteTex, v_uv);
    int k = int(params.x + 0.5) - 1;
    float v = (k & 1) != 0 ? m.a : dot(m.rgb, vec3(0.2126, 0.7152, 0.0722));
    o = texture(tex, v_uv) * clamp((k & 2) != 0 ? 1.0 - v : v, 0.0, 1.0);
}
