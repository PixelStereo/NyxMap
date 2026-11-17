#version 440
// A triangle over the target; its first row holds the picture's bottom (RhiDevice.h)
layout(location = 0) out vec2 v_uv;
layout(std140, binding = 0) uniform Blit { vec4 flip; };
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    v_uv = p * 0.5 + 0.5;
    gl_Position = vec4(p.x, p.y * flip.x, 0.0, 1.0);
}
