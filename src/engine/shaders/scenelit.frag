#version 440
// The lit scenes (the port of Model3D.cpp's kFragment, step by step): physically based (GGX, Smith, Schlick), in linear
// light: its base colour (times its texture's), metal and roughness (factors, times their textures'), normals (its normal
// map, from the screen-space frame), occlusion, emission; under the ambient light, up to eight lights (directional, point,
// spot, area, as many as the scene has) and the studio's light; exposure, ACES tone mapping, sRGB out. Not yet here: shadows, the environment,
// screen-space occlusion and reflections, the glow.
layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec3 v_n;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_basePos;
layout(location = 4) in float v_heightScale;
layout(location = 5) in vec2 v_heightSlope;
layout(location = 0) out vec4 o;
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
};
// The lights, as many as the scene has: seven vec4 each. color (xyz: times its power; w: its kind, 0 directional, 1 point,
// 2 spot, 3 area), position, direction, right and up (an area's sides), half its size (xy), cone (x: a spot's angle, y:
// where its falloff begins, in radians)
layout(std430, binding = 9) readonly buffer Lights { vec4 lights[]; };
#define u_eye u_eye4.xyz
#define u_ambient u_ambient4.xyz
#define u_exposure u_misc.x
#define u_studio u_misc.y
#define u_count u_counts.x
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
float noise() { return fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))); }
float lit(int i, vec3 N) { return 1.0; } // shadows: the next step
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
vec4 reflected(vec3 R, float rough) { return vec4(0.0); } // reflections: a later step
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
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
            return;
        }
        base *= vec4(pow(t.rgb, vec3(2.2)), t.a);
    } else if (u_unlit != 0) {
        o = vec4(u_base.rgb * u_base.a, u_base.a);
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
    float contact = 1.0; // occlusion between surfaces: a later step
    ao *= contact;
    float r = clamp(rough, 0.04, 1.0), a = r * r;
    vec3 F0 = mix(vec3(u_dielectric), base.rgb, metal);
    float NdV = max(dot(N, V), 1e-4);
    vec3 c = u_ambient * base.rgb * (1.0 - 0.7 * metal) * ao;
    if (u_studio > 0.0)    if (u_studio > 0.0) { // no environment: the studio's light
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
    c = aces(shining);
    // Dithered: no bands in the gradients of a spot on a floor. Premultiplied, as a group's picture
    o = vec4(clamp(pow(c, vec3(1.0 / 2.2)) + (noise() - 0.5) / 255.0, 0.0, 1.0) * base.a, base.a);
}
