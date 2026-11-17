#version 440
// A triangle over the picture; v_ndc: where it is, from -1 to 1 (the first row at the bottom)
layout(location = 0) out vec2 v_ndc;
layout(std140, binding = 0) uniform Post { vec4 flip; vec4 grade; vec4 more; }; // flip.x: the picture's flip; grade: vignette, contrast, saturation, temperature
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    v_ndc = p;
    gl_Position = vec4(p.x, p.y * flip.x, 0.0, 1.0);
}
