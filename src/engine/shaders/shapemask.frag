#version 440
// A picture through its surface masks' alpha (Masks.h): the alpha only, or the color too (a viewport: shown opaque)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform ShapeMask { vec4 params; }; // x: the color goes through too
layout(binding = 1) uniform sampler2D tex;
layout(binding = 2) uniform sampler2D mask; // stored top row first
void main() {
    vec4 c = texture(tex, v_uv);
    float m = texture(mask, vec2(v_uv.x, 1.0 - v_uv.y)).r;
    o = params.x > 0.5 ? c * m : vec4(c.rgb, c.a * m);
}
