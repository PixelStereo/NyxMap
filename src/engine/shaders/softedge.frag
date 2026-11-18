#version 440
// A viewport's output as it goes to its projector: faded towards each side so that it blends into the next projector's
// picture (soft edge), its black lifted outside the overlap, and, to line the projectors up, a guide in place of the picture.
//
// The fade is Paul Bourke's blend: in linear light the pictures of two overlapping projectors add up to one:
// w(x) = 0.5 (2x)^p for the first half of the ramp, 1 - 0.5 (2 (1 - x))^p for the second (x: 0 at the edge, 1 at the end of
// the ramp); it is then raised to 1 / gamma, as the projector's own response is gamma, so that what is lit is w.
//
// Black level: a projector never shows a true black, so the overlap, lit by two, has a black twice as bright as the rest. The
// black outside the ramps is lifted to match (in linear light, white stays white).
//
// The guide (modes): 1 a pattern: the overlap of each side in this viewport's color, a grid, the ends of the ramps and the
// diagonals (two projectors that line up make their two colors one white band, a grid that runs through both); 2 white, 3 grey,
// 4 black: uniform fields to judge the curve, the gamma and the black level.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform SoftEdge {
    vec4 width;  // pixels of the ramp: left, right, top, bottom (0: none)
    vec4 curve;  // the exponent p of each side
    vec4 more;   // x: the projector's gamma, y: the ramp applied (1) or not (0, to see the guide whole), z: the black level (0..1)
    vec4 size;   // the output's width and height in pixels
    vec4 probe;  // the pointer: x, y in the picture (0..1, origin bottom left; negative: none)
    vec4 guide;  // x: the guide's mode (0: none), y: only the overlap (1)
    vec4 gcolor; // rgb: this viewport's color in the guide
};
layout(binding = 1) uniform sampler2D tex;
float ramp(float distance, float length, float p) {
    if (length <= 0.0) return 1.0;
    float x = clamp(distance / length, 0.0, 1.0);
    return x < 0.5 ? 0.5 * pow(2.0 * x, p) : 1.0 - 0.5 * pow(2.0 * (1.0 - x), p);
}
// 1 on a line of `thick` pixels every `step` pixels
float lines(vec2 px, float step, float thick) {
    vec2 m = mod(px, step);
    vec2 d = min(m, step - m);
    return min(d.x, d.y) < thick ? 1.0 : 0.0;
}
vec3 pattern(vec2 px, bool zone) {
    bool only = guide.y > 0.5;
    vec3 col = zone ? gcolor.rgb * 0.5 : (only ? vec3(0.0) : vec3(0.09));
    if (zone || !only) {
        float minor = lines(px, 32.0, 0.6), major = lines(px, 128.0, 1.1);
        col = mix(col, zone ? vec3(0.95) : vec3(0.45), minor * (zone ? 0.55 : 0.5));
        col = mix(col, zone ? vec3(1.0) : vec3(0.85), major);
        // the diagonals of the whole output
        float d1 = abs(px.y * size.x - px.x * size.y) / length(size.xy);
        float d2 = abs((size.y - px.y) * size.x - px.x * size.y) / length(size.xy);
        if (min(d1, d2) < 0.9) col = mix(col, vec3(0.8), 0.7);
    }
    // the end of each ramp, and the sides of the output
    float edge = 1e9;
    if (width.x > 0.0) edge = min(edge, abs(px.x - width.x));
    if (width.y > 0.0) edge = min(edge, abs(size.x - px.x - width.y));
    if (width.z > 0.0) edge = min(edge, abs(size.y - px.y - width.z));
    if (width.w > 0.0) edge = min(edge, abs(px.y - width.w));
    if (edge < 1.6) col = vec3(1.0);
    if (min(min(px.x, size.x - px.x), min(px.y, size.y - px.y)) < 1.5) col = mix(col, gcolor.rgb, 0.9);
    return col;
}
void main() {
    vec2 px = v_uv * size.xy;
    bool zone = (width.x > 0.0 && px.x < width.x) || (width.y > 0.0 && size.x - px.x < width.y) ||
                (width.z > 0.0 && size.y - px.y < width.z) || (width.w > 0.0 && px.y < width.w);
    vec4 c = texture(tex, v_uv);
    vec3 rgb = c.rgb;
    int mode = int(guide.x + 0.5);
    if (mode == 1) rgb = pattern(px, zone);
    else if (mode == 2) rgb = vec3(1.0);
    else if (mode == 3) rgb = vec3(0.5);
    else if (mode == 4) rgb = vec3(0.0);
    float w = ramp(px.x, width.x, curve.x) * ramp(size.x - px.x, width.y, curve.y) * ramp(size.y - px.y, width.z, curve.z) *
              ramp(px.y, width.w, curve.w);
    if (more.y > 0.5) rgb *= pow(w, 1.0 / max(more.x, 1.0));
    if (more.z > 0.0 && !zone) { // the black of one projector made that of two
        vec3 lin = pow(rgb, vec3(max(more.x, 1.0)));
        rgb = pow(lin * (1.0 - more.z) + more.z, vec3(1.0 / max(more.x, 1.0)));
    }
    if (probe.x >= 0.0) { // a cross over the whole output through the pointer: white, outlined in black
        vec2 d = abs(px - probe.xy * size.xy);
        float line = min(d.x, d.y);
        rgb = line < 1.5 ? vec3(1.0) : line < 2.5 ? vec3(0.0) : rgb;
    }
    o = vec4(rgb, mode > 0 ? 1.0 : c.a);
}
