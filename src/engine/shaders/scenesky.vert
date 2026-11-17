#version 440
// A triangle over the picture at the far plane
layout(location = 0) out vec2 v_ndc;
layout(std140, binding = 0) uniform Post { vec4 flip; vec4 grade; vec4 more; };
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    v_ndc = p;
    gl_Position = vec4(p.x, p.y * flip.x, 1.0, 1.0);
}
