#version 440
// The scene's background (premultiplied) in the picture, and nothing in what shines: the second attachment cannot be cleared
// to another colour than the first
layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 o;
layout(location = 1) out vec4 o_bright;
layout(std140, binding = 1) uniform Background { vec4 color; };
void main() {
    o = color;
    o_bright = vec4(0.0);
}
