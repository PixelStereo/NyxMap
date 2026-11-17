#version 440
// An effect's result over its input: through its mask (luminance times alpha, stretched), at its mix, on the channels
// it changes only
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform EffectMask {
    vec4 channels;
    vec4 params; // x: mix, y: mask present, z: inverted
};
layout(binding = 1) uniform sampler2D inTex;
layout(binding = 2) uniform sampler2D fxTex;
layout(binding = 3) uniform sampler2D maskTex;
void main() {
    float k = 1.0;
    if (params.y > 0.5) {
        vec4 m = texture(maskTex, v_uv);
        k = clamp(dot(m.rgb, vec3(0.2126, 0.7152, 0.0722)) * m.a, 0.0, 1.0);
        if (params.z > 0.5) k = 1.0 - k;
    }
    vec4 a = texture(inTex, v_uv);
    o = mix(a, mix(a, texture(fxTex, v_uv), k * params.x), channels);
}
