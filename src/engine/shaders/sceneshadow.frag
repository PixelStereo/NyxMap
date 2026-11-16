#version 440
// A light's view of the models: the depth, in a float (a spot's or a directional light's map)
layout(location = 0) in vec3 v_pos;
layout(location = 0) out vec4 o;
void main() {
    // A bias along the slope, as a polygon offset gives (the depth test is made here, in the main pass)
    float z = gl_FragCoord.z, slope = max(abs(dFdx(z)), abs(dFdy(z)));
    o = vec4(z + 1.5 * slope + 1.2e-7);
}
