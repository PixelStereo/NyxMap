#version 440
// A layer composited by Difference: against a copy of what is already drawn, which blending cannot give
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_comp;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform Composite {
    vec4 view;
    vec4 rot;
    vec4 misc;
    vec4 matte;
};
layout(binding = 1) uniform sampler2D tex;
layout(binding = 2) uniform sampler2D mask;     // composition masks, stored top row first
layout(binding = 3) uniform sampler2D matteTex; // a matte in Composition space
layout(binding = 4) uniform sampler2D dst;      // the target so far, the same size
float isfMatte() {
    int mode = int(matte.x + 0.5);
    if (mode == 0) return 1.0;
    vec4 m = texture(matteTex, v_comp);
    int k = mode - 1;
    float v = (k & 1) != 0 ? m.a : dot(m.rgb, vec3(0.2126, 0.7152, 0.0722));
    return clamp((k & 2) != 0 ? 1.0 - v : v, 0.0, 1.0);
}
void main() {
    vec4 c = texture(tex, v_uv);
    float a = c.a * misc.z * isfMatte();
    if (misc.w > 0.5) a *= texture(mask, vec2(v_comp.x, 1.0 - v_comp.y)).r;
    vec4 d = texelFetch(dst, ivec2(gl_FragCoord.xy), 0);
    o = vec4(abs(d.rgb - c.rgb * a), d.a + a * (1.0 - d.a));
}
