#version 440
// A plain copy (between formats and sizes: sampled, linear)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(binding = 1) uniform sampler2D picture;
void main() { o = texture(picture, v_uv); }
