#version 440
layout(location = 0) in vec2 position; // NDC, y up (the matrix corrects it for the backend)
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(std140, binding = 0) uniform U {
    mat4 clip;
};
void main()
{
    v_uv = uv;
    v_color = color;
    gl_Position = clip * vec4(position, 0.0, 1.0);
}
