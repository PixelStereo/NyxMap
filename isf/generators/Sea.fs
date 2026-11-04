/*{
	"ISFVSN": "2",
	"DESCRIPTION": "A dark sea seen from above, foam around a black monolith; what the kick does to it in hit",
	"CREDIT": "Pixel Stereo",
	"CATEGORIES": ["Generator"],
	"INPUTS": [
		{"NAME": "scale", "LABEL": "Scale", "TYPE": "float", "DEFAULT": 3.2, "MIN": 0.3, "MAX": 20},
		{"NAME": "speed", "LABEL": "Speed", "TYPE": "float", "DEFAULT": 0.12, "MIN": 0, "MAX": 2},
		{"NAME": "wake", "LABEL": "Foam", "TYPE": "float", "DEFAULT": 0.6, "MIN": 0, "MAX": 2},
		{"NAME": "slab", "LABEL": "Monolith", "TYPE": "float", "DEFAULT": 1, "MIN": 0, "MAX": 1},
		{"NAME": "hit", "LABEL": "Hit", "TYPE": "float", "DEFAULT": 0, "MIN": 0, "MAX": 1},
		{"NAME": "spark", "LABEL": "Sparkles", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0, "MAX": 3},
		{"NAME": "warm", "LABEL": "Warm", "TYPE": "float", "DEFAULT": 0, "MIN": 0, "MAX": 1}
	]
}*/
float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float noise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
	float v = 0.0, a = 0.5;
	for (int k = 0; k < 5; ++k) { v += a * noise(p); p = p * 2.03 + vec2(13.1, 7.7); a *= 0.5; }
	return v;
}
void main() {
	vec2 uv = isf_FragNormCoord - 0.5;
	uv.x *= RENDERSIZE.x / RENDERSIZE.y;
	float t = TIME * speed;
	vec2 p = uv * scale;
	vec2 q = vec2(fbm(p + t), fbm(p + vec2(5.2, 1.3) - t));
	float h = fbm(p + 2.6 * q + vec2(t * 0.6, -t));
	// Its slab, long and thin; the foam goes around it
	vec2 s = abs(uv) - vec2(0.045, 0.34);
	float d = length(max(s, 0.0)) + min(max(s.x, s.y), 0.0);
	float ring = exp(-max(d, 0.0) * mix(13.0, 5.0, hit)) * (wake + hit * 1.2);
	float filaments = smoothstep(0.52, 0.56, abs(sin((h + q.x) * 21.0 + d * 26.0 - TIME * 1.4)) * (0.35 + h));
	float foam = filaments * clamp(ring, 0.0, 1.0) + smoothstep(0.72, 0.84, h) * 0.25;
	vec3 deep = vec3(0.012, 0.045, 0.06), teal = vec3(0.05, 0.22, 0.25);
	vec3 c = mix(deep, teal, smoothstep(0.25, 0.8, h));
	c = mix(c, c * vec3(1.9, 0.75, 0.45), warm);
	c += vec3(0.8, 0.95, 1.0) * foam;
	c += vec3(1.0) * spark * pow(noise(p * 34.0 + TIME * 3.0), 16.0) * smoothstep(0.45, 0.75, h) * 2.5;
	c = mix(c, vec3(0.006), slab * smoothstep(0.006, -0.002, d));
	gl_FragColor = vec4(c, 1.0);
}
