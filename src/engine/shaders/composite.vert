#version 440
// A layer's mapped mesh (composition clip space, y up) into the part of the composition the target shows, turned
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec2 v_comp; // where in the composition (0..1, origin bottom left)
layout(std140, binding = 0) uniform Composite {
    vec4 view;  // scale, offset: the part of the composition the target shows
    vec4 rot;   // that part turned: cos, sin, center (clip space)
    vec4 misc;  // x: composition width / height, y: NDC flip, z: opacity, w: composition masks on
    vec4 matte; // x: its matte's mode (0: none)
};
void main() {
    v_uv = a_uv;
    v_comp = a_pos * 0.5 + 0.5;
    vec2 q = a_pos - rot.zw;
    q.x *= misc.x;
    q = vec2(rot.x * q.x - rot.y * q.y, rot.y * q.x + rot.x * q.y);
    q.x /= misc.x;
    vec2 p = (q + rot.zw) * view.xy + view.zw;
    gl_Position = vec4(p.x, p.y * misc.y, 0.0, 1.0);
}
