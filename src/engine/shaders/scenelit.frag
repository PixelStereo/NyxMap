#version 440
// The lit scenes (the port of Model3D.cpp's kFragment, step by step): physically based (GGX, Smith, Schlick), in linear
// light: its base colour (times its texture's), metal and roughness (factors, times their textures'), normals (its normal
// map, from the screen-space frame), occlusion, emission; under the ambient light, up to eight lights (directional, point,
// spot, area, as many as the scene has), their shadows and the studio's light; exposure, ACES tone mapping, sRGB out. Not yet here: shadows, the environment,
// screen-space occlusion and reflections, the glow.
layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec3 v_n;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_basePos;
layout(location = 4) in float v_heightScale;
layout(location = 5) in vec2 v_heightSlope;
layout(location = 0) out vec4 o;
layout(location = 1) out vec4 o_bright; // what shines more than a screen can show: its glow (linear light)
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
layout(std140, binding = 1) uniform Scene {
    vec4 u_eye4;       // xyz: the camera
    vec4 u_ambient4;   // xyz: the ambient light, times its intensity
    vec4 u_misc;       // x: exposure; y: the studio's strength
    ivec4 u_counts;    // x: how many lights
    ivec4 u_shadowCfg; // x: soft edges (penumbra); y: taps per shadow
    vec4 u_screenInfo; // xy: the picture's size; z: how much occlusion; w: how much reflection
    ivec4 u_reflectCfg; // x: steps along a reflected ray; y: a last picture to reflect
    vec4 u_range;      // x: the near plane; y: the far plane; z: the exposure the last picture was made with
    mat4 u_viewProj;
    vec4 u_envInfo;    // x: its intensity; y: its rotation; z: its blurriest level; w: its values are linear light
    ivec4 u_envFlags;  // x: an environment
};
// The lights, as many as the scene has: seven vec4 each. color (xyz: times its power; w: its kind, 0 directional, 1 point,
// 2 spot, 3 area), position, direction, right and up (an area's sides), half its size (xy), cone (x: a spot's angle, y:
// where its falloff begins, in radians)
layout(std430, binding = 9) readonly buffer Lights { vec4 lights[]; };
// The shadows: a spot's or a directional light's map: six vec4 (the matrix as four columns, the distance planes, then its
// edge radius and bias); a point light's: two (position and far plane, then its softness)
layout(std430, binding = 10) readonly buffer Shadows { vec4 shadowData[]; };
layout(std430, binding = 11) readonly buffer PointShadows { vec4 pointData[]; };
layout(binding = 12) uniform sampler2DArray u_shadowMaps; // each light's view of the models: the depth, in a float
layout(binding = 13) uniform sampler2DArray u_pointMaps;
layout(binding = 14) uniform sampler2D u_aoTex;  // the occlusion between surfaces, from what is seen
layout(binding = 15) uniform sampler2D u_seen;   // the depth of what is seen
layout(binding = 16) uniform sampler2D u_last;   // the last picture: what a reflection shows
layout(binding = 17) uniform sampler2D u_env;    // the environment: its picture in half floats, with its blurred levels  // a point light's: the distance / its far plane, six faces each
#define u_eye u_eye4.xyz
#define u_ambient u_ambient4.xyz
#define u_exposure u_misc.x
#define u_studio u_misc.y
#define u_count u_counts.x
#define u_penumbra u_shadowCfg.x
#define u_envIntensity u_envInfo.x
#define u_envRot u_envInfo.y
#define u_envMaxLod u_envInfo.z
#define u_envLinear int(u_envInfo.w)
#define u_hasEnv u_envFlags.x
#define u_screen u_screenInfo.xy
#define u_aoAmount u_screenInfo.z
#define u_reflections u_screenInfo.w
#define u_steps u_reflectCfg.x
#define u_hasLast u_reflectCfg.y
#define u_depthRange u_range.xy
#define u_lastExposure u_range.z
#define u_glowLimit u_range.w
#define u_taps u_shadowCfg.y
layout(binding = 3) uniform sampler2D u_tex;
layout(binding = 4) uniform sampler2D u_roughTex;
layout(binding = 5) uniform sampler2D u_metalTex;
layout(binding = 6) uniform sampler2D u_normalTex;
layout(binding = 7) uniform sampler2D u_aoTexMap;
layout(binding = 8) uniform sampler2D u_emTex;
#define u_hasTex u_flags.x
#define u_layerTex u_flags.y
#define u_alphaMode u_flags.z
#define u_unlit u_flags.w
#define u_alphaCutoff u_emissive4.w
#define u_emissive u_emissive4.xyz
#define u_metal u_mat0.x
#define u_rough u_mat0.y
#define u_dielectric u_mat0.z
#define u_normalScale u_mat0.w
#define u_aoStrength u_mat1.x
#define u_hasRough u_has.x
#define u_hasMetal u_has.y
#define u_hasNormal u_has.z
#define u_hasAo u_has.w
#define u_hasEm u_has2.x
const float PI = 3.14159265;
// What shines more than the threshold, to spread around it
vec4 bright(vec3 c, float a) {
    float l = max(c.r, max(c.g, c.b));
    return vec4(min(c * max(l - u_glowLimit, 0.0) / max(l, 1e-4), vec3(64.0)) * a, a);
}
float noise() { return fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))); }
// One look at a map: the share of the 4 texels around a point where the light gets through, interpolated as a hardware
// comparison would (the maps are floats)
float tap(sampler2DArray maps, vec2 uv, float layer, float ref) {
    vec2 size = vec2(textureSize(maps, 0).xy);
    vec2 p = uv * size - 0.5;
    vec2 f = fract(p);
    vec4 g = textureGather(maps, vec3((floor(p) + 1.0) / size, layer), 0); // (i0, j1), (i1, j1), (i1, j0), (i0, j0)
    vec4 c = vec4(lessThanEqual(vec4(ref), g));
    return mix(mix(c.w, c.z, f.x), mix(c.x, c.y, f.x), f.y);
}
// Distance from a light, from what its map holds
float mapDistance(vec3 p, float z) {
    return p.z > 0.0 ? p.x + z * (p.y - p.x) : p.x * p.y / (p.y - z * (p.y - p.x));
}
const vec3 kLook[6] = vec3[6](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));
const vec3 kUp[6] = vec3[6](vec3(0, -1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1), vec3(0, -1, 0), vec3(0, -1, 0));
// A point light's map at a direction: the face it falls on, and where on it (the faces are 90° views)
vec3 pointFace(vec3 q, int slot) {
    vec3 a = abs(q);
    int face = a.x >= a.y && a.x >= a.z ? (q.x > 0.0 ? 0 : 1) : (a.y >= a.z ? (q.y > 0.0 ? 2 : 3) : (q.z > 0.0 ? 4 : 5));
    vec3 look = kLook[face], up = kUp[face], right = cross(look, up);
    float d = dot(q, look);
    return vec3(vec2(dot(q, right), dot(q, up)) / d * 0.5 + 0.5, float(slot * 6 + face));
}
float pointRaw(vec3 q, int slot) { vec3 f = pointFace(q, slot); return textureLod(u_pointMaps, f, 0.0).r; }
float lit(int i, vec3 N) {
    int s = int(lights[i * 7 + 2].w);
    int p = int(lights[i * 7 + 1].w);
    if (p >= 0) { // a point light: the distances all around it
        vec3 at = lights[i * 7 + 1].xyz;
        float far = pointData[p * 2].w, soft = pointData[p * 2 + 1].x;
        vec3 d = v_pos + N * 0.012 * sqrt(max(length(v_pos - at), 0.1)) - at;
        float ref = length(d) / far - 0.0006;
        vec3 x = normalize(cross(d, abs(d.y) > 0.9 * length(d) ? vec3(1, 0, 0) : vec3(0, 1, 0))), y = normalize(cross(d, x));
        float angle = noise() * 6.2831853, sum = 0.0, spread = soft * 0.0018 * length(d);
        if (u_penumbra != 0 && spread > 0.0) { // sharp where what stops the light is near, soft where it is far
            float found = 0.0, blockers = 0.0;
            for (int k = 0; k < 8; ++k) {
                float rr = sqrt((float(k) + 0.5) / 8.0), aa = float(k) * 2.39996323; // the same at each pixel: an even edge
                vec3 q = normalize(d + (x * cos(aa) + y * sin(aa)) * rr * spread * 4.0);
                float there = pointRaw(q, p);
                if (there < ref) { found += there; blockers += 1.0; }
            }
            if (blockers == 0.0) return 1.0;
            float near = found / blockers;
            spread *= clamp((ref - near) / max(near, 1e-3) * 4.0 + 0.25, 0.25, 2.0);
        }
        for (int k = 0; k < u_taps; ++k) {
            float rr = sqrt((float(k) + 0.5) / float(u_taps)), aa = angle + float(k) * 2.39996323;
            vec3 q = d + (x * cos(aa) + y * sin(aa)) * rr * spread;
            vec3 f = pointFace(q, p);
            sum += tap(u_pointMaps, f.xy, f.z, ref);
        }
        return sum / float(u_taps);
    }
    if (s < 0) return 1.0;
    mat4 shadowMat = mat4(shadowData[s * 6], shadowData[s * 6 + 1], shadowData[s * 6 + 2], shadowData[s * 6 + 3]);
    vec3 planes = shadowData[s * 6 + 4].xyz;
    float radius = shadowData[s * 6 + 5].x, bias = shadowData[s * 6 + 5].y;
    vec4 q = shadowMat * vec4(v_pos + N * bias, 1.0);
    vec3 t = q.xyz / q.w * 0.5 + 0.5;
    if (q.w <= 0.0 || t.x <= 0.0 || t.x >= 1.0 || t.y <= 0.0 || t.y >= 1.0 || t.z >= 1.0) return 1.0;
    float angle = noise() * 6.2831853, sum = 0.0;
    float c = cos(angle), si = sin(angle);
    // Sharp where what stops the light is near, soft where it is far. An area's is wide: as few looks as this would draw
    // contours in it, all of it as soft
    if (u_penumbra != 0 && radius > 0.0 && int(lights[i * 7].w) != 3) {
        float found = 0.0, blockers = 0.0;
        for (int k = 0; k < 8; ++k) {
            float rr = sqrt((float(k) + 0.5) / 8.0), aa = float(k) * 2.39996323;
            vec2 d = vec2(cos(aa), sin(aa)) * rr;
            float there = textureLod(u_shadowMaps, vec3(t.xy + d * radius * 4.0, float(s)), 0.0).r; // the same at each pixel: an even edge
            if (there < t.z) { found += mapDistance(planes, there); blockers += 1.0; }
        }
        if (blockers == 0.0) return 1.0;
        float near = found / blockers;
        radius *= clamp((mapDistance(planes, t.z) - near) / max(near, 1e-3) * 4.0 + 0.25, 0.25, 2.0);
    }
    int taps = int(lights[i * 7].w) == 3 ? u_taps * 2 : u_taps; // its edge is wide: more of them, or it shows grain
    for (int k = 0; k < taps; ++k) {
        float rr = sqrt((float(k) + 0.5) / float(taps)), aa = float(k) * 2.39996323;
        vec2 d = vec2(cos(aa), sin(aa)) * rr;
        sum += tap(u_shadowMaps, t.xy + vec2(c * d.x - si * d.y, si * d.x + c * d.y) * radius, float(s), t.z);
    }
    return sum / float(taps);
}
float panel(vec3 d, vec3 at, float size, float blur) { return smoothstep(size - blur, size + blur * 0.4, dot(d, normalize(at))); }
// A photo studio: a large panel above, a smaller one behind on each side, a dark room; blurred as the surface is rough
vec3 studio(vec3 d, float r) {
    float b = 0.05 + r * r * 0.9;
    vec3 c = mix(vec3(0.012, 0.012, 0.014), vec3(0.075, 0.08, 0.09), smoothstep(-0.25, 0.9, d.y));
    c += vec3(1.0, 0.97, 0.92) * 2.4 * panel(d, vec3(0.25, 0.9, 0.35), 0.86, b);
    c += vec3(0.8, 0.88, 1.0) * 1.2 * panel(d, vec3(-0.85, 0.25, -0.45), 0.94, b);
    c += vec3(1.0, 0.9, 0.82) * 0.8 * panel(d, vec3(0.9, 0.05, 0.3), 0.97, b);
    return c * u_studio;
}
// The reverse of what makes a pixel: tone mapping and display encoding (a reflection reads the last frame's)
vec3 unprocessed(vec3 c) {
    c = pow(clamp(c, 0.0, 0.985), vec3(2.2));
    vec3 a = 2.51 - 2.43 * c, b = 0.03 - 0.59 * c, d = -0.14 * c;
    return (-b + sqrt(max(b * b - 4.0 * a * d, vec3(0.0)))) / (2.0 * a) / max(u_lastExposure, 1e-3);
}
float eyeDepth(float z) { return u_depthRange.x * u_depthRange.y / (u_depthRange.y - z * (u_depthRange.y - u_depthRange.x)); }
// What the mirror direction meets on the screen; its weight in w (0: nothing)
vec4 reflected(vec3 R, float rough) {
    if (u_hasLast == 0 || u_reflections <= 0.0 || rough > 0.65) return vec4(0.0);
    // Coming back towards the camera, it would look for what the screen does not show: the far side of things
    float away = smoothstep(0.25, -0.1, dot(R, normalize(u_eye - v_pos)));
    if (away <= 0.0) return vec4(0.0);
    float span = length(u_eye - v_pos) * 0.05;
    vec3 p = v_pos + R * span * 0.3;
    float jitter = 0.9 + 0.2 * noise();
    float grow = pow(1.07, 40.0 / float(u_steps)); // reaching as far, in fewer steps or more
    span *= 40.0 / float(u_steps);
    for (int k = 0; k < u_steps; ++k) {
        p += R * span * jitter;
        span *= grow;
        vec4 q = u_viewProj * vec4(p, 1.0);
        if (q.w <= 0.0) return vec4(0.0);
        vec3 s = q.xyz / q.w * 0.5 + 0.5;
        if (s.x <= 0.0 || s.x >= 1.0 || s.y <= 0.0 || s.y >= 1.0 || s.z >= 1.0) return vec4(0.0);
        float seen = textureLod(u_seen, s.xy, 0.0).r;
        if (seen >= 1.0) continue; // the sky: nothing to meet
        float behind = eyeDepth(s.z) - eyeDepth(seen);
        if (behind > 0.0) {
            if (behind > span * 1.2) continue; // behind a surface too far in front: passing under it
            for (int b = 0; b < 5; ++b) { // where it crossed
                span *= 0.5;
                p -= R * span * 0.5 * (behind > 0.0 ? 1.0 : -1.0);
                q = u_viewProj * vec4(p, 1.0);
                s = q.xyz / q.w * 0.5 + 0.5;
                behind = eyeDepth(s.z) - eyeDepth(textureLod(u_seen, s.xy, 0.0).r);
            }
            vec4 c = textureLod(u_last, s.xy, 0.0);
            if (c.a <= 0.0) return vec4(0.0);
            vec2 edge = smoothstep(vec2(0.0), vec2(0.08), s.xy) * smoothstep(vec2(0.0), vec2(0.08), 1.0 - s.xy);
            float fade = away * edge.x * edge.y * (1.0 - float(k) / float(u_steps)) * smoothstep(0.65, 0.15, rough);
            return vec4(unprocessed(c.rgb / c.a), fade * u_reflections);
        }
    }
    return vec4(0.0);
}
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
vec3 env(vec3 d, float lod) {
    float c = cos(u_envRot), s = sin(u_envRot);
    d = vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
    vec2 uv = vec2(0.5 + atan(d.x, -d.z) / (2.0 * PI), acos(clamp(d.y, -1.0, 1.0)) / PI);
    vec3 e = textureLod(u_env, vec2(uv.x, 1.0 - uv.y), lod).rgb; // bottom up, as a layer's picture
    return (u_envLinear != 0 ? e : pow(e, vec3(2.2))) * u_envIntensity; // an HDR file: linear already
}
vec2 envBrdf(float NdV, float r) { // its response to an environment (Karis' approximation of the BRDF table)
    vec4 k = r * vec4(-1.0, -0.0275, -0.572, 0.022) + vec4(1.0, 0.0425, 1.04, -0.04);
    float a004 = min(k.x * k.x, exp2(-9.28 * NdV)) * k.x + k.y;
    return vec2(-1.04, 1.04) * a004 + k.zw;
}
vec3 smoothDisplacedNormal(vec3 n) {
    // Project the interpolated texture slope onto the base surface.
    // Differentiating displaced positions would give one normal per face.
    vec2 uv = u_hasDisp == 2 ? v_uv : vec2(v_uv.x / u_uvTiling.x, 1.0 - v_uv.y / u_uvTiling.y);
    vec3 rx = cross(dFdy(v_basePos), n), ry = cross(n, dFdx(v_basePos));
    float area = dot(dFdx(v_basePos), rx);
    if (abs(area) < 1e-12) return n;
    vec3 gradient = (rx * dot(v_heightSlope, dFdx(uv)) + ry * dot(v_heightSlope, dFdy(uv))) / area;
    return normalize(n - gradient * (u_dispAmount * v_heightScale));
}
void main() {
    vec4 base = u_base;
    if (u_hasTex != 0) {
        vec4 t = texture(u_tex, u_layerTex != 0 ? vec2(v_uv.x / u_uvTiling.x, 1.0 - v_uv.y / u_uvTiling.y) : v_uv); // a layer's picture: bottom up
        if (u_unlit != 0) { // as it is: a screen
            o = vec4(t.rgb * u_base.rgb * t.a * u_base.a, t.a * u_base.a);
            o_bright = bright(pow(t.rgb * u_base.rgb, vec3(2.2)), t.a * u_base.a);
            return;
        }
        base *= vec4(pow(t.rgb, vec3(2.2)), t.a);
    } else if (u_unlit != 0) {
        o = vec4(u_base.rgb * u_base.a, u_base.a);
        o_bright = bright(pow(u_base.rgb, vec3(2.2)), u_base.a);
        return;
    }
    if (u_alphaMode == 1 && base.a < u_alphaCutoff) discard;
    if (u_alphaMode == 0) base.a = 1.0;
    vec3 N = normalize(v_n);
    if (u_hasDisp != 0 && u_shading == 1)
        N = smoothDisplacedNormal(N);
    else if (u_hasDisp != 0 || u_shading == 2)
        N = normalize(cross(dFdx(v_pos), dFdy(v_pos)));
    vec3 V = normalize(u_eye - v_pos);
    if (dot(N, V) < 0.0) N = -N; // both faces lit
    if (u_hasNormal != 0) { // its normal map, in the frame its UVs make on screen
        vec3 dp1 = dFdx(v_pos), dp2 = dFdy(v_pos);
        vec2 du1 = dFdx(v_uv), du2 = dFdy(v_uv);
        vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
        vec3 T = dp2perp * du1.x + dp1perp * du2.x, B = dp2perp * du1.y + dp1perp * du2.y;
        float invmax = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-12);
        vec3 m = texture(u_normalTex, v_uv).xyz * 2.0 - 1.0;
        m.xy *= u_normalScale;
        N = normalize(mat3(T * invmax, -B * invmax, N) * m);
    }
    float rough = u_rough * (u_hasRough != 0 ? dot(texture(u_roughTex, v_uv), u_roughCh) : 1.0);
    float metal = u_metal * (u_hasMetal != 0 ? dot(texture(u_metalTex, v_uv), u_metalCh) : 1.0);
    float ao = u_hasAo != 0 ? mix(1.0, texture(u_aoTexMap, v_uv).r, u_aoStrength) : 1.0;
    float contact = u_aoAmount > 0.0 ? texture(u_aoTex, gl_FragCoord.xy / u_screen).r : 1.0;
    ao *= contact;
    float r = clamp(rough, 0.04, 1.0), a = r * r;
    vec3 F0 = mix(vec3(u_dielectric), base.rgb, metal);
    float NdV = max(dot(N, V), 1e-4);
    vec3 c = u_ambient * base.rgb * (1.0 - 0.7 * metal) * ao;
    if (u_hasEnv != 0) { // the environment: diffuse from its most blurred level, reflections as rough as the surface
        vec2 ab = envBrdf(NdV, r);
        vec3 R = reflect(-V, N);
        vec4 near = reflected(R, r);
        vec3 spec = mix(env(R, r * (u_envMaxLod - 2.0)), near.rgb, near.a) * (F0 * ab.x + ab.y);
        vec3 diff = env(N, u_envMaxLod - 1.5) * base.rgb * (1.0 - metal);
        c += (diff + spec) * ao;
    } else if (u_studio > 0.0) { // no environment: the studio's light
        vec2 ab = envBrdf(NdV, r);
        vec3 R = reflect(-V, N);
        vec4 near = reflected(R, r);
        c += (mix(studio(R, r), near.rgb, near.a) * (F0 * ab.x + ab.y) + studio(N, 1.0) * 0.3 * base.rgb * (1.0 - metal)) * ao;
    } else {
        vec2 ab = envBrdf(NdV, r);
        vec4 near = reflected(reflect(-V, N), r);
        c += near.rgb * near.a * (F0 * ab.x + ab.y) * ao;
    }
    for (int i = 0; i < u_count; ++i) {
        vec3 L, Lspec; float att = 1.0, aSpec = a;
        if (int(lights[i * 7].w) == 0) L = normalize(-lights[i * 7 + 2].xyz);
        else if (int(lights[i * 7].w) == 3) { // an area: diffuse from its nearest point, reflections from where the mirror direction meets it
            vec3 n = normalize(lights[i * 7 + 2].xyz), toP = v_pos - lights[i * 7 + 1].xyz;
            vec2 q = clamp(vec2(dot(toP, lights[i * 7 + 3].xyz), dot(toP, lights[i * 7 + 4].xyz)), -lights[i * 7 + 5].xy, lights[i * 7 + 5].xy);
            vec3 d = lights[i * 7 + 1].xyz + lights[i * 7 + 3].xyz * q.x + lights[i * 7 + 4].xyz * q.y - v_pos;
            float dist2 = max(dot(d, d), 1e-4);
            L = d * inversesqrt(dist2);
            att = max(dot(-L, n), 0.0) / dist2;
            vec3 R = reflect(-V, N);
            float along = dot(R, n);
            Lspec = L;
            if (along < -1e-3) {
                vec3 hit = v_pos + R * (dot(lights[i * 7 + 1].xyz - v_pos, n) / along) - lights[i * 7 + 1].xyz;
                vec2 h = clamp(vec2(dot(hit, lights[i * 7 + 3].xyz), dot(hit, lights[i * 7 + 4].xyz)), -lights[i * 7 + 5].xy, lights[i * 7 + 5].xy);
                Lspec = normalize(lights[i * 7 + 1].xyz + lights[i * 7 + 3].xyz * h.x + lights[i * 7 + 4].xyz * h.y - v_pos);
            }
            aSpec = min(a + length(lights[i * 7 + 5].xy) * inversesqrt(dist2) * 0.5, 1.0);
        } else {
            vec3 d = lights[i * 7 + 1].xyz - v_pos;
            float dist2 = max(dot(d, d), 1e-4);
            L = d * inversesqrt(dist2);
            att = 1.0 / dist2;
            if (int(lights[i * 7].w) == 2) att *= 1.0 - smoothstep(lights[i * 7 + 6].y, lights[i * 7 + 6].x, acos(clamp(dot(-L, normalize(lights[i * 7 + 2].xyz)), -1.0, 1.0))); // by its angle: an even gradient
        }
        float NdL = max(dot(N, L), 0.0);
        if (NdL <= 0.0 || att <= 0.0) continue; // nothing comes here: no need to know what stops it
        if (int(lights[i * 7].w) != 3) Lspec = L;
        float NdS = int(lights[i * 7].w) == 3 ? max(dot(N, Lspec), 1e-3) : NdL;
        vec3 H = normalize(Lspec + V);
        float NdH = max(dot(N, H), 0.0), VdH = max(dot(V, H), 0.0);
        float dd = NdH * NdH * (aSpec * aSpec - 1.0) + 1.0;
        float D = aSpec * aSpec / (dd * dd) * (a / aSpec) * (a / aSpec);
        float k = (r + 1.0) * (r + 1.0) / 8.0;
        float G = (NdV / (NdV * (1.0 - k) + k)) * (NdS / (NdS * (1.0 - k) + k));
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdH, 5.0);
        vec3 spec = D * G * F / (4.0 * NdV * NdS + 1e-4);
        vec3 kd = (1.0 - F) * (1.0 - metal);
        c += (kd * base.rgb * NdL + spec * NdS) * lights[i * 7 + 0].xyz * att * lit(i, N) * mix(1.0, contact, 0.5);
    }
    vec3 em = u_emissive * (u_hasEm != 0 ? pow(texture(u_emTex, v_uv).rgb, vec3(2.2)) : vec3(1.0));
    vec3 shining = (c + em) * u_exposure;
    o_bright = bright(shining, base.a);
    c = aces(shining);
    // Dithered: no bands in the gradients of a spot on a floor. Premultiplied, as a group's picture
    o = vec4(clamp(pow(c, vec3(1.0 / 2.2)) + (noise() - 0.5) / 255.0, 0.0, 1.0) * base.a, base.a);
}
