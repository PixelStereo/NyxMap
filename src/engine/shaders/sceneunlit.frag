#version 440
// The unlit look (the port of Model3D.cpp's kUnlitFragment): the models' colours and pictures as they are, premultiplied
layout(location = 2) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform Place {
    mat4 u_model;
    mat4 u_view;
    mat4 u_proj;
    mat3 u_normal;
    vec4 u_tiling;     // x, y: the UV tiling; z: the displacement's amount; w: its middle
    ivec4 u_ints;      // x: hasDisp; y: shading; z: dispTopology; w: the shape FX's count
    int u_fxKind[8];   // its shape FX, in the order they move its points
    vec4 u_fxA[8];
    vec4 u_fxB[8];
    vec4 u_fxCenter4;  // of its bounds: what Twist, Taper, Wave and Noise measure against
    vec4 u_fxSize4;
    vec4 u_base;       // the material, for the flat colour and the unlit look
    vec4 u_emissive4;  // xyz: emission; w: the alpha cut-off
    ivec4 u_flags;     // x: hasTex; y: layerTex; z: alphaMode
    vec4 u_ink;        // the plain look's colour
};
#define u_uvTiling u_tiling.xy
#define u_dispAmount u_tiling.z
#define u_dispMid u_tiling.w
#define u_hasDisp u_ints.x
#define u_shading u_ints.y
#define u_dispTopology u_ints.z
#define u_fxCount u_ints.w
#define u_fxCenter u_fxCenter4.xyz
#define u_fxSize u_fxSize4.xyz
layout(binding = 2) uniform sampler2D u_tex;
#define u_hasTex u_flags.x
#define u_layerTex u_flags.y
#define u_alphaMode u_flags.z
#define u_alphaCutoff u_emissive4.w
#define u_emissive u_emissive4.xyz
void main() {
    vec4 c = u_base;
    if (u_hasTex != 0) {
        vec4 t = texture(u_tex, u_layerTex != 0 ? vec2(v_uv.x / u_uvTiling.x, 1.0 - v_uv.y / u_uvTiling.y) : v_uv); // a layer's picture: bottom up
        c *= vec4(pow(t.rgb, vec3(2.2)), t.a);
    }
    if (u_alphaMode == 1 && c.a < u_alphaCutoff) discard;
    if (u_alphaMode == 0) c.a = 1.0;
    o = vec4(pow(clamp(c.rgb + u_emissive, 0.0, 1.0), vec3(1.0 / 2.2)) * c.a, c.a); // premultiplied
}
