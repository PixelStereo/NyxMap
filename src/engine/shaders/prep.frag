#version 440
// Layer preparation: its ROI (part of the source used: a rectangle, a triangle, a quad), its color (added, removed,
// balanced, through a mask), un-premultiplied alpha (groups)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform Prep {
    vec4 roi;      // the rectangle: left, bottom, right, top (texture coordinates)
    vec4 add;
    vec4 remove;
    vec4 balance;
    vec4 modes;    // x: ROI mode (0 rectangle, 1 triangle, 2 quad, 3 none), y: un-premultiply, z: mask (0 none, 1, 2 inverted)
    vec4 tri01;    // the triangle's points (top-left origin)
    vec4 tri2;
    vec4 quad0, quad1, quad2; // the quad's homography, by columns
};
layout(binding = 1) uniform sampler2D tex;
layout(binding = 2) uniform sampler2D maskTex;
float side(vec2 a, vec2 b, vec2 p) { vec2 e = b - a, d = p - a; return e.x * d.y - e.y * d.x; }
void main() {
    vec2 uv = mix(roi.xy, roi.zw, v_uv);
    int roiMode = int(modes.x + 0.5);
    if (roiMode == 3) { o = vec4(0.0); return; }
    if (roiMode == 1) {
        vec2 p = vec2(uv.x, 1.0 - uv.y);
        float a = side(tri01.xy, tri01.zw, p), b = side(tri01.zw, tri2.xy, p), c = side(tri2.xy, tri01.xy, p);
        if (min(a, min(b, c)) < 0.0 && max(a, max(b, c)) > 0.0) { o = vec4(0.0); return; }
    }
    if (roiMode == 2) {
        vec3 p = mat3(quad0.xyz, quad1.xyz, quad2.xyz) * vec3(v_uv.x, 1.0 - v_uv.y, 1.0);
        uv = vec2(p.x / p.z, 1.0 - p.y / p.z);
    }
    vec4 c = texture(tex, uv);
    if (modes.y > 0.5 && c.a > 0.0) c.rgb /= c.a;
    vec3 col = clamp(c.rgb * balance.rgb * (1.0 - remove.rgb) + add.rgb, 0.0, 1.0);
    int maskMode = int(modes.z + 0.5);
    if (maskMode != 0) {
        vec4 m = texture(maskTex, v_uv);
        float k = clamp(dot(m.rgb, vec3(0.2126, 0.7152, 0.0722)) * m.a, 0.0, 1.0);
        if (maskMode == 2) k = 1.0 - k;
        col = mix(clamp(c.rgb, 0.0, 1.0), col, k);
    }
    o = vec4(col, c.a);
}
