#version 440
// VideoTexture's conversion: the decoder's planes (stored top row first) into RGBA
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform Conversion {
    vec4 sel[4];
    vec4 off[4];
    vec4 packed3;
    vec4 scale;
    vec4 m0, m1, m2;
    vec4 range;
    vec4 crop;
    vec4 mode;
};
// The texture of each output channel (HAP: 1 its first texture, 2 its second)
layout(binding = 1) uniform sampler2D t0;
layout(binding = 2) uniform sampler2D t1;
layout(binding = 3) uniform sampler2D t2;
layout(binding = 4) uniform sampler2D t3;

// One channel: sampled (subsampled chroma interpolated), or, from a plane of n channels laid out as one channel n
// times as wide, read texel by texel (such planes are never subsampled: the pass draws one pixel per texel group)
float channel(sampler2D t, vec2 uv, vec2 shift, vec4 select, float packed)
{
    if (packed > 0.5) {
        int n = int(packed + 0.5);
        ivec2 size = textureSize(t, 0);
        int w = size.x / n;
        ivec2 px = ivec2(min(uv * vec2(w, size.y), vec2(w - 1, size.y - 1)));
        int k = int(dot(select, vec4(0.0, 1.0, 2.0, 3.0)) + 0.5);
        return texelFetch(t, ivec2(px.x * n + k, px.y), 0).r;
    }
    return dot(texture(t, uv + shift), select);
}

void main()
{
    vec2 uv = min(vec2(v_uv.x, 1.0 - v_uv.y) * crop.xy, crop.zw);
    int m = int(mode.x + 0.5);
    if (m >= 3) {
        vec4 c = texture(t0, uv);
        if (m == 3) { o = vec4(clamp(c.rgb, 0.0, 1.0), mode.y > 0.5 ? c.a : 1.0); return; }
        if (m == 6) { o = vec4(c.rrr, 1.0); return; }
        float s = c.z * (255.0 / 8.0) + 1.0;
        float co = (c.x - 128.0 / 255.0) / s, cg = (c.y - 128.0 / 255.0) / s;
        vec3 rgb = vec3(c.w + co - cg, c.w + cg, c.w - co - cg);
        o = vec4(clamp(rgb, 0.0, 1.0), m == 5 ? texture(t1, uv).r : 1.0);
        return;
    }
    float a = channel(t0, uv, off[0].xy, sel[0], packed3.x) * scale.x;
    float b = channel(t1, uv, off[1].xy, sel[1], packed3.y) * scale.y;
    float c = channel(t2, uv, off[2].xy, sel[2], packed3.z) * scale.z;
    float alpha = mode.y > 0.5 ? channel(t3, uv, off[3].xy, sel[3], packed3.w) * scale.w : 1.0;
    vec3 v = vec3((a - range.x) * range.y, (b - range.z) * range.w, (c - range.z) * range.w);
    vec3 rgb = m == 0 ? vec3(dot(m0.xyz, v), dot(m1.xyz, v), dot(m2.xyz, v)) : m == 1 ? vec3(a, b, c) : vec3(a);
    o = vec4(clamp(rgb, 0.0, 1.0), clamp(alpha, 0.0, 1.0));
}
