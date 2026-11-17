#version 440
// The 3D scene views' vertices (the port of Model3D.cpp's kVertex): a model's points through its shape FX and its
// displacement, placed, seen by the camera. The point's world position and normal go on for the fragment shaders.
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_n;
layout(location = 2) in vec2 a_uv;
layout(location = 0) out vec3 v_pos;
layout(location = 1) out vec3 v_n;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec3 v_basePos;
layout(location = 4) out float v_heightScale;
layout(location = 5) out vec2 v_heightSlope;
layout(std140, binding = 0) uniform Place {
    mat4 u_model;
    mat4 u_view;
    mat4 u_proj;
    mat3 u_normal;
    vec4 u_tiling;     // x, y: the UV tiling; z: the displacement's amount; w: its middle
    ivec4 u_ints;      // x: hasDisp; y: shading; z: dispTopology; w: the shape FX's count
    int u_fxKind[8];   // its shape FX, in the order they move its points
    vec4 u_fxA[8];
    vec4 u_fxB[8];
    vec4 u_fxCenter4;  // of its bounds: what Twist, Taper, Wave and Noise measure against
    vec4 u_fxSize4;
    vec4 u_base;       // the material
    vec4 u_emissive4;  // xyz: emission; w: the alpha cut-off
    ivec4 u_flags;     // x: hasTex; y: layerTex; z: alphaMode; w: unlit
    vec4 u_ink;        // the plain look's colour
    vec4 u_mat0;       // metal, rough, dielectric (reflectance at 0), normalScale
    vec4 u_mat1;       // aoStrength
    ivec4 u_has;       // roughness, metal, normal, occlusion maps
    ivec4 u_has2;      // emission map
    vec4 u_roughCh;    // the channel of the roughness map, metal map
    vec4 u_metalCh;
};
#define u_uvTiling u_tiling.xy
#define u_dispAmount u_tiling.z
#define u_dispMid u_tiling.w
#define u_hasDisp u_ints.x
#define u_shading u_ints.y
#define u_dispTopology u_ints.z
#define u_fxCount u_ints.w
#define u_fxCenter u_fxCenter4.xyz
#define u_fxSize u_fxSize4.xyz
layout(binding = 2) uniform sampler2D u_disp;
float rawHeight(vec2 uv) {
    vec4 h = textureLod(u_disp, uv, 0.0);
    return dot(h.rgb, vec3(0.2126, 0.7152, 0.0722)) * h.a;
}
float stitchedU(vec2 uv) {
    if ((u_dispTopology & 1) == 0) return rawHeight(uv);
    uv.x = fract(uv.x);
    float distanceToEdge = min(uv.x, 1.0 - uv.x);
    float blend = 1.0 - smoothstep(0.0, 0.10, distanceToEdge);
    if (blend <= 0.0) return rawHeight(uv);
    float seam = 0.5 * (rawHeight(vec2(0, uv.y)) + rawHeight(vec2(1, uv.y)));
    return mix(rawHeight(uv), seam, blend);
}
float heightAt(vec2 uv) {
    if (u_hasDisp != 1 || u_dispTopology == 0) return rawHeight(uv);
    if ((u_dispTopology & 2) != 0) {
        uv.y = fract(uv.y);
        float edge = min(uv.y, 1.0 - uv.y);
        float blend = 1.0 - smoothstep(0.0, 0.10, edge);
        if (blend <= 0.0) return stitchedU(uv);
        float seam = 0.5 * (stitchedU(vec2(uv.x, 0)) + stitchedU(vec2(uv.x, 1)));
        return mix(stitchedU(uv), seam, blend);
    }
    uv.y = clamp(uv.y, 0.0, 1.0);
    float pole = uv.y < 0.5 ? 0.0 : 1.0;
    int poleFlag = pole == 0.0 ? 4 : 8;
    float blend = 1.0 - smoothstep(0.0, 0.14, abs(uv.y - pole));
    if ((u_dispTopology & poleFlag) == 0 || blend <= 0.0) return stitchedU(uv);
    // A whole UV row converges on one vertex position at the pole.
    float mean = 0.0;
    for (int i = 0; i < 8; ++i) mean += rawHeight(vec2((float(i) + 0.5) / 8.0, pole));
    return mix(stitchedU(uv), mean / 8.0, blend);
}
vec3 bendPosition(vec3 p, float r) { // around an upright axis in front of it, across its width
    float t = (p.x - u_fxCenter.x) / r;
    return vec3(u_fxCenter.x + (r - p.z) * sin(t), p.y, r - (r - p.z) * cos(t));
}
float fxCoord(vec3 q, int ax) { return ax == 0 ? q.x : ax == 1 ? q.y : q.z; }
float fxLength(int ax) { return max(ax == 0 ? u_fxSize.x : ax == 1 ? u_fxSize.y : u_fxSize.z, 1e-6); }
float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float valueNoise(vec3 x) { // smooth, from -1 to 1
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash31(i), b = hash31(i + vec3(1, 0, 0)), c = hash31(i + vec3(0, 1, 0)), d = hash31(i + vec3(1, 1, 0));
    float e = hash31(i + vec3(0, 0, 1)), g = hash31(i + vec3(1, 0, 1)), h = hash31(i + vec3(0, 1, 1)), k = hash31(i + vec3(1, 1, 1));
    return mix(mix(mix(a, b, f.x), mix(c, d, f.x), f.y), mix(mix(e, g, f.x), mix(h, k, f.x), f.y), f.z) * 2.0 - 1.0;
}
vec3 turnAround(vec3 q, int ax, float angle) {
    float c = cos(angle), s = sin(angle);
    if (ax == 0) return vec3(q.x, c * q.y - s * q.z, s * q.y + c * q.z);
    if (ax == 1) return vec3(c * q.x + s * q.z, q.y, c * q.z - s * q.x);
    return vec3(c * q.x - s * q.y, s * q.x + c * q.y, q.z);
}
// One shape FX on a point: 2 Twist (a: radians end to end, axis), 3 Taper (amount, axis), 4 Wave (amplitude, length, phase,
// axis it travels along: it moves the points across, along the next axis), 5 Noise (a: amount, features per length, how far
// it is into its step, how a step becomes the next: 0 not at all, 1 linearly, 2 smoothly; b: the step, the seed)
vec3 noiseField(vec3 x, float step) { // a field of noise of its own at each step
    vec3 o = vec3(step * 7.19, step * 3.83, step * 9.41);
    return vec3(valueNoise(x + o), valueNoise(x + o + vec3(17.3, 5.1, 9.7)), valueNoise(x + o + vec3(3.9, 21.7, 13.1)));
}
vec3 shapeOp(int kind, vec4 a, vec4 b, vec3 p) {
    vec3 q = p - u_fxCenter;
    if (kind == 2) {
        int ax = int(a.y + 0.5);
        q = turnAround(q, ax, a.x * fxCoord(q, ax) / fxLength(ax));
    } else if (kind == 3) {
        int ax = int(a.y + 0.5);
        float k = max(1.0 + a.x * 2.0 * fxCoord(q, ax) / fxLength(ax), 0.0);
        if (ax == 0) q.yz *= k; else if (ax == 1) q.zx *= k; else q.xy *= k;
    } else if (kind == 4) {
        int ax = int(a.w + 0.5), across = (ax + 1) % 3;
        float s = sin(6.2831853 * (fxCoord(q, ax) / max(a.y, 1e-6) + a.z)) * a.x;
        if (across == 0) q.x += s; else if (across == 1) q.y += s; else q.z += s;
    } else if (kind == 5) {
        vec3 x = q / max(max(u_fxSize.x, u_fxSize.y), max(u_fxSize.z, 1e-6)) * a.y + vec3(b.y * 11.3, b.y * 5.7, b.y * 8.9);
        float f = a.w < 0.5 ? 0.0 : (a.w < 1.5 ? a.z : a.z * a.z * (3.0 - 2.0 * a.z));
        q += mix(noiseField(x, b.x), noiseField(x, b.x + 1.0), f) * a.x;
    }
    return q + u_fxCenter;
}
void main() {
    vec3 p = a_pos, n = a_n, base = a_pos;
    v_heightSlope = vec2(0.0);
    for (int i = 0; i < u_fxCount; ++i) {
        int kind = u_fxKind[i];
        if (kind == 0) {
            if (u_hasDisp != 0) {
                vec2 uv = u_hasDisp == 2 ? a_uv * u_uvTiling : vec2(a_uv.x, 1.0 - a_uv.y);
                p += normalize(n) * (heightAt(uv) - u_dispMid) * u_dispAmount;
                if (u_shading == 1) {
                    vec2 stepUV = 1.0 / vec2(textureSize(u_disp, 0));
                    vec2 dx = vec2(stepUV.x, 0), dy = vec2(0, stepUV.y);
                    v_heightSlope = vec2(heightAt(uv + dx) - heightAt(uv - dx),
                                         heightAt(uv + dy) - heightAt(uv - dy)) / (2.0 * stepUV);
                }
            }
        } else if (kind == 1) {
            float bend = u_fxA[i].x; // radians across its width
            if (abs(bend) > 1e-4) {
                float R = u_fxSize.x / bend, t = (p.x - u_fxCenter.x) / R, s = sin(t), c = cos(t);
                p = bendPosition(p, R);
                base = bendPosition(base, R);
                n = vec3(n.x * c - n.z * s, n.y, n.x * s + n.z * c);
            }
        } else {
            // Its normal follows what the FX does to its surface: the inverse transpose of the FX's slope, from three small steps
            vec4 a = u_fxA[i], b = u_fxB[i];
            vec3 q = shapeOp(kind, a, b, p);
            float e = 0.001 * max(max(u_fxSize.x, u_fxSize.y), u_fxSize.z);
            vec3 jx = (shapeOp(kind, a, b, p + vec3(e, 0, 0)) - q) / e;
            vec3 jy = (shapeOp(kind, a, b, p + vec3(0, e, 0)) - q) / e;
            vec3 jz = (shapeOp(kind, a, b, p + vec3(0, 0, e)) - q) / e;
            vec3 c = cross(jy, jz) * n.x + cross(jz, jx) * n.y + cross(jx, jy) * n.z;
            if (dot(jx, cross(jy, jz)) < 0.0) c = -c;
            if (dot(c, c) > 1e-18) n = normalize(c);
            p = q;
            base = shapeOp(kind, a, b, base);
        }
    }
    vec4 w = u_model * vec4(p, 1.0);
    v_pos = w.xyz; v_n = u_normal * n; v_uv = a_uv * u_uvTiling;
    v_basePos = (u_model * vec4(base, 1.0)).xyz;
    v_heightScale = 1.0 / max(length(u_normal * normalize(n)), 1e-8);
    gl_Position = u_proj * u_view * w;
}
