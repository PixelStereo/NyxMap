#version 440
// IEC sRGB transfer functions (Khronos Data Format, section 13.3): a source's pixels decoded (straight alpha), or a
// viewport's picture decoded, exposed and encoded (opaque)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform OutputColor {
    vec4 params; // x: opacity, y: gain, z: decode, w: encode
    vec4 more;   // x: a source (straight alpha kept)
};
layout(binding = 1) uniform sampler2D tex;
vec3 decodeSrgb(vec3 c) { return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, lessThanEqual(c, vec3(0.04045))); }
vec3 encodeSrgb(vec3 c) { return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, 12.92 * c, lessThanEqual(c, vec3(0.0031308))); }
// D65 linear Display P3 <-> Rec.709 matrices (CSS Color 4 reference primaries)
const mat3 p3To709 = mat3(1.224940176, -0.042056955, -0.019637555, -0.224940176, 1.042056955, -0.078636046, 0.0, 0.0, 1.098273601);
const mat3 rec709ToP3 = mat3(0.822461968, 0.033194199, 0.017082631, 0.177538032, 0.966805801, 0.072397441, 0.0, 0.0, 0.910519929);
void main() {
    vec4 c = texture(tex, v_uv);
    bool source = more.x > 0.5;
    int decode = int(params.z + 0.5), encode = int(params.w + 0.5);
    vec3 rgb = max(c.rgb * (source ? 1.0 : c.a * params.x), vec3(0.0));
    if (decode == 0 || decode == 2) rgb = decodeSrgb(rgb);
    else if (decode == 3) rgb = pow(rgb, vec3(2.4));
    // ITU-R BT.709-6 inverse OETF: distinct from sRGB and display Gamma 2.4
    else if (decode == 4) rgb = mix(pow((rgb + 0.099) / 1.099, vec3(1.0 / 0.45)), rgb / 4.5, lessThan(rgb, vec3(0.081)));
    if (decode == 2) rgb = p3To709 * rgb;
    rgb *= params.y;
    if (encode == 2) rgb = rec709ToP3 * rgb;
    rgb = max(rgb, vec3(0.0));
    if (encode == 0 || encode == 2) rgb = encodeSrgb(rgb);
    else if (encode == 3) rgb = pow(rgb, vec3(1.0 / 2.4));
    o = vec4(clamp(rgb, 0.0, 1.0), source ? c.a : 1.0);
}
