#version 440
// The air in front of what is seen (the port of Model3D.cpp's kFogFragment): walked from the camera, the light each step
// receives (stopped by the models where the shadow maps say) and what the air hides. In rgb what it adds, in a what is
// still seen through it
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 o;
layout(std140, binding = 1) uniform Fog { mat4 u_inv; vec4 u_eye4; vec4 u_fogColor4; vec4 u_a; vec4 u_b; ivec4 u_ints; };
#define u_eye u_eye4.xyz
#define u_fogColor u_fogColor4.xyz
#define u_density u_a.x
#define u_fogBase u_a.y
#define u_falloff u_a.z
#define u_reach u_a.w
#define u_exposure u_b.x
#define u_steps u_ints.x
#define u_count u_ints.y
layout(binding = 2) uniform sampler2D u_seen;
layout(std430, binding = 9) readonly buffer Lights { vec4 lights[]; };
layout(std430, binding = 10) readonly buffer Shadows { vec4 shadowData[]; };
layout(std430, binding = 11) readonly buffer PointShadows { vec4 pointData[]; };
layout(binding = 12) uniform sampler2DArray u_shadowMaps;
layout(binding = 13) uniform sampler2DArray u_pointMaps;
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
float noise() { return fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))); }
float through(int i, vec3 p) {
    int s = int(lights[i * 7 + 2].w), c = int(lights[i * 7 + 1].w);
    if (c >= 0) {
        vec3 d = p - lights[i * 7 + 1].xyz;
        float ref = length(d) / pointData[c * 2].w - 0.0006;
        vec3 f = pointFace(d, c);
        return tap(u_pointMaps, f.xy, f.z, ref);
    }
    if (s < 0) return 1.0;
    mat4 shadowMat = mat4(shadowData[s * 6], shadowData[s * 6 + 1], shadowData[s * 6 + 2], shadowData[s * 6 + 3]);
    vec4 q = shadowMat * vec4(p, 1.0);
    vec3 t = q.xyz / q.w * 0.5 + 0.5;
    if (q.w <= 0.0 || t.x <= 0.0 || t.x >= 1.0 || t.y <= 0.0 || t.y >= 1.0 || t.z >= 1.0) return 1.0;
    return tap(u_shadowMaps, t.xy, float(s), t.z);
}
void main() {
    vec2 uv = v_ndc * 0.5 + 0.5;
    vec4 w = u_inv * vec4(v_ndc, texture(u_seen, uv).r * 2.0 - 1.0, 1.0);
    vec3 to = w.xyz / w.w - u_eye;
    float far = min(length(to), u_reach), ds = far / float(u_steps);
    vec3 dir = normalize(to);
    vec3 added = vec3(0.0);
    float seen = 1.0, t = ds * noise();
    for (int k = 0; k < u_steps; ++k, t += ds) {
        vec3 p = u_eye + dir * t;
        float air = u_density * exp(-u_falloff * max(p.y - u_fogBase, 0.0));
        float gets = air * ds;
        vec3 light = u_fogColor;
        for (int i = 0; i < u_count; ++i) {
            if (dot((lights[i * 7].xyz * lights[i * 7 + 5].z), (lights[i * 7].xyz * lights[i * 7 + 5].z)) <= 0.0) continue;
            vec3 L = -lights[i * 7 + 2].xyz;
            float att = 1.0;
            if (int(lights[i * 7].w) != 0) {
                vec3 d = lights[i * 7 + 1].xyz - p;
                float dist2 = max(dot(d, d), 0.05);
                L = d * inversesqrt(dist2);
                att = 1.0 / dist2;
                if (int(lights[i * 7].w) == 2) {
                    // The angular gradient only needs acos along the edge of the beam.
                    float axis = dot(-L, normalize(lights[i * 7 + 2].xyz));
                    if (axis <= cos(lights[i * 7 + 6].x)) continue;
                    if (axis < cos(lights[i * 7 + 6].y))
                        att *= 1.0 - smoothstep(lights[i * 7 + 6].y, lights[i * 7 + 6].x, acos(clamp(axis, -1.0, 1.0)));
                }
                if (int(lights[i * 7].w) == 3) att *= max(dot(-L, normalize(lights[i * 7 + 2].xyz)), 0.0);
                if (att <= 1e-4) continue;
            }
            float phaseBase = 1.16 - 0.8 * dot(dir, -L);
            float phase = 0.0796 * (1.0 - 0.16) / (phaseBase * sqrt(phaseBase)); // forward, as dust
            light += (lights[i * 7].xyz * lights[i * 7 + 5].z) * att * phase * through(i, p);
        }
        added += seen * gets * light;
        seen *= exp(-gets);
    }
    o = vec4(added * u_exposure, seen);
}
