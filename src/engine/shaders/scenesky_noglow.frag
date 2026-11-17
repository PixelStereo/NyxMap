#version 440
// The environment behind the models (the port of Model3D.cpp's kSkyFragment): its direction from the camera, through the
// inverse of its matrices
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 o;
layout(std140, binding = 1) uniform Sky { mat4 u_inv; vec4 u_eye4; vec4 u_params; vec4 u_more; };
#define u_eye u_eye4.xyz
#define u_envIntensity u_params.x
#define u_envRot u_params.y
#define u_lod u_params.z
#define u_exposure u_params.w
#define u_envLinear int(u_more.x)
#define u_glowLimit u_more.y
layout(binding = 2) uniform sampler2D u_env;
const float PI = 3.14159265;
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
void main() {
    vec4 p = u_inv * vec4(v_ndc, 1.0, 1.0);
    vec3 d = normalize(p.xyz / p.w - u_eye);
    float c = cos(u_envRot), s = sin(u_envRot);
    d = vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
    vec2 uv = vec2(0.5 + atan(d.x, -d.z) / (2.0 * PI), acos(clamp(d.y, -1.0, 1.0)) / PI);
    vec3 e = textureLod(u_env, vec2(uv.x, 1.0 - uv.y), u_lod).rgb;
    e = (u_envLinear != 0 ? e : pow(e, vec3(2.2))) * u_envIntensity;
    vec3 shining = e * u_exposure;
    o = vec4(pow(aces(shining), vec3(1.0 / 2.2)), 1.0);
}
