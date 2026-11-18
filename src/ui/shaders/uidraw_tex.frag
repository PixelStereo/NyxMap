#version 440
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform U {
    mat4 clip;
};
layout(binding = 1) uniform sampler2D picture;
void main()
{
    vec4 t = texture(picture, v_uv);
    // v_color.rgb: the channel shown (RhiDraw::setChannel): all 1 the picture, one 1 that channel, all 0 the alpha
    float sum = v_color.r + v_color.g + v_color.b;
    if (sum > 2.5) {
        o = vec4(t.rgb, mix(t.a, 1.0, v_color.a));
    } else {
        float g = sum < 0.5 ? t.a : dot(t.rgb, v_color.rgb);
        o = vec4(vec3(g), 1.0);
    }
}
