#version 440
// The depth of what the camera sees, in a float (what the occlusion and the reflections read)
layout(location = 0) in vec3 v_pos;
layout(location = 0) out vec4 o;
void main() { o = vec4(gl_FragCoord.z); }
