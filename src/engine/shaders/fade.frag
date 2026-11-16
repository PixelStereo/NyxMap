#version 440
// A crossfade between two pictures (a missing one is transparent black), mixed in the shader rather than by blending
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform Fade { vec4 params; }; // x: the progress, 0 the first picture, 1 the second
layout(binding = 1) uniform sampler2D a;
layout(binding = 2) uniform sampler2D b;
void main() {
    o = mix(texture(a, v_uv), texture(b, v_uv), params.x);
}
