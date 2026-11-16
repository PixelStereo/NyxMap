#version 440
// All over the 3D picture, as a camera does (the port of Model3D.cpp's kPostFragment): the colours, the darker edges.
// The air and the glow are not here yet
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform Post { vec4 flip; vec4 grade; vec4 more; };
layout(binding = 1) uniform sampler2D u_pic;
#define u_vignette grade.x
#define u_contrast grade.y
#define u_saturation grade.z
#define u_temperature grade.w
float noise() { return fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))); }
void main() {
    vec2 uv = v_ndc * 0.5 + 0.5;
    vec4 c = texture(u_pic, uv);
    if (c.a > 0.0) {
        vec3 rgb = c.rgb / c.a;
        rgb *= vec3(1.0 + u_temperature * 0.2, 1.0, 1.0 - u_temperature * 0.2);
        float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        rgb = mix(vec3(luma), rgb, u_saturation);
        rgb = (rgb - 0.5) * u_contrast + 0.5;
        c.rgb = max(rgb, 0.0) * c.a;
    }
    c.rgb *= 1.0 - u_vignette * smoothstep(0.45, 1.45, length(v_ndc));
    o = clamp(vec4(c.rgb + (noise() - 0.5) / 255.0, c.a), 0.0, 1.0);
}
