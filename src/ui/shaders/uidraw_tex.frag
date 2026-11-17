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
    o = vec4(t.rgb, mix(t.a, 1.0, v_color.a));
}
