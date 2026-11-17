#version 440
// A triangle over the picture, drawn so that its first row holds the picture's bottom (RhiDevice.h)
layout(location = 0) out vec2 v_uv;
layout(std140, binding = 0) uniform Conversion {
    vec4 sel[4];   // which channel of its texture each output channel reads (Y U V A, R G B A, grey and A)
    vec4 off[4];   // chroma siting, in texture coordinates (xy)
    vec4 packed3;  // per output channel: its plane's channels (3, 4) when laid out as one channel that many times wide
    vec4 scale;    // the value read is multiplied by this (bits held in 16)
    vec4 m0, m1, m2; // the YUV matrix, by rows
    vec4 range;    // Y offset, Y scale, C offset, C scale
    vec4 crop;     // xy: part of the textures holding the picture; zw: never beyond its last texel centre (HAP)
    vec4 mode;     // x: 0 YUV, 1 RGB, 2 grey, 3 HAP RGB(A), 4 scaled YCoCg, 5 scaled YCoCg + alpha, 6 alpha only;
                   // y: alpha used; z: NDC flip
};
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    v_uv = p * 0.5 + 0.5;
    gl_Position = vec4(p.x, p.y * mode.z, 0.0, 1.0);
}
