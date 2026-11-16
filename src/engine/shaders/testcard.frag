#version 440
// A viewport's test card: its still picture (top row first) and its motion: a sweep turning clockwise in the circle once
// every 4 s with a fading trail, and a white block going to and fro in its band. Coordinates in pixels, origin bottom left.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform Card {
    vec4 timeSize; // x: the time (s), yz: the size
    vec4 circle;   // centre, radius
    vec4 band;     // x0, x1, y0, y1
};
layout(binding = 1) uniform sampler2D still;
void main() {
    vec2 p = v_uv * timeSize.yz;
    vec3 c = texture(still, vec2(v_uv.x, 1.0 - v_uv.y)).rgb;
    vec2 d = p - circle.xy;
    if (length(d) < circle.z) {
        float s = mod(-timeSize.x * 1.5707963, 6.2831853);
        float behind = mod(atan(d.y, d.x) - s, 6.2831853);
        c = mix(c, vec3(0.25, 1.0, 0.45), 0.6 * exp(-behind * 2.5));
    }
    float w = (band.y - band.x) * 0.06;
    float x = band.x + (band.y - band.x - w) * (0.5 + 0.5 * sin(timeSize.x * 2.0));
    if (p.y >= band.z && p.y <= band.w && p.x >= x && p.x <= x + w) c = vec3(1.0);
    o = vec4(c, 1.0);
}
