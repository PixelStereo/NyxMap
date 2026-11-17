#version 440
// A viewport's output faded towards each side so that it blends into the next projector's picture (soft edge). The fade is Paul Bourke's blend: in linear light the pictures
// of two overlapping projectors add up to one: w(x) = 0.5 (2x)^p for the first half of the ramp, 1 - 0.5 (2 (1 - x))^p for
// the second (x: 0 at the edge, 1 at the end of the ramp); it is then raised to 1 / gamma, as the projector's own response
// is gamma, so that what is lit is w
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform SoftEdge {
    vec4 width; // pixels of the ramp: left, right, top, bottom (0: none)
    vec4 curve; // the exponent p of each side
    vec4 more;  // x: the projector's gamma
    vec4 size;  // the output's width and height in pixels
    vec4 probe; // the pointer: x, y in the picture (0..1, origin bottom left; negative: none)
};
layout(binding = 1) uniform sampler2D tex;
float ramp(float distance, float length, float p) {
    if (length <= 0.0) return 1.0;
    float x = clamp(distance / length, 0.0, 1.0);
    float w = x < 0.5 ? 0.5 * pow(2.0 * x, p) : 1.0 - 0.5 * pow(2.0 * (1.0 - x), p);
    return w;
}
// v_uv.y is 0 at the picture's bottom (fullscreen.vert)
void main() {
    vec2 px = v_uv * size.xy;
    float w = ramp(px.x, width.x, curve.x) * ramp(size.x - px.x, width.y, curve.y) * ramp(size.y - px.y, width.z, curve.z) *
              ramp(px.y, width.w, curve.w);
    vec4 c = texture(tex, v_uv);
    vec3 rgb = c.rgb * pow(w, 1.0 / max(more.x, 1.0));
    if (probe.x >= 0.0) { // a cross over the whole output through the pointer: white, outlined in black
        vec2 d = abs(px - probe.xy * size.xy);
        float line = min(d.x, d.y);
        rgb = line < 1.5 ? vec3(1.0) : line < 2.5 ? vec3(0.0) : rgb;
    }
    o = vec4(rgb, c.a);
}
