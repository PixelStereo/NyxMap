#version 440
// The plain look: one colour (the ink)
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
    vec4 u_base;       // the material
    vec4 u_emissive4;  // xyz: emission; w: the alpha cut-off
    ivec4 u_flags;     // x: hasTex; y: layerTex; z: alphaMode; w: unlit
    vec4 u_ink;        // the plain look's colour
    vec4 u_mat0;       // metal, rough, dielectric (reflectance at 0), normalScale
    vec4 u_mat1;       // aoStrength
    ivec4 u_has;       // roughness, metal, normal, occlusion maps
    ivec4 u_has2;      // emission map
    vec4 u_roughCh;    // the channel of the roughness map, metal map
    vec4 u_metalCh;
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
void main() { o = u_ink; }
