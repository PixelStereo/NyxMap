#version 440
// A point light's view of the models: their distance from it, over its far plane (the light's place and that distance
// come in the ink of the block of sceneview.vert)
layout(location = 0) in vec3 v_pos;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform Place {
    mat4 u_model; mat4 u_view; mat4 u_proj; mat3 u_normal; vec4 u_tiling; ivec4 u_ints; int u_fxKind[8]; vec4 u_fxA[8]; vec4 u_fxB[8];
    vec4 u_fxCenter4; vec4 u_fxSize4; vec4 u_base; vec4 u_emissive4; ivec4 u_flags;
    vec4 u_ink; // xyz: the light; w: its far plane
    vec4 u_mat0; vec4 u_mat1; ivec4 u_has; ivec4 u_has2; vec4 u_roughCh; vec4 u_metalCh;
};
void main() { o = vec4(length(v_pos - u_ink.xyz) / u_ink.w); }
