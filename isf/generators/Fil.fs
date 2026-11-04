/*{
	"ISFVSN": "2",
	"DESCRIPTION": "The thread: one white line that vibrates with the sound, that can split",
	"CREDIT": "Pixel Stereo",
	"CATEGORIES": ["Generator", "Audio Visualizer"],
	"INPUTS": [
		{"NAME": "wave", "TYPE": "audio", "MAX": 256},
		{"NAME": "x", "LABEL": "Where", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0, "MAX": 1},
		{"NAME": "reach", "LABEL": "How Long", "TYPE": "float", "DEFAULT": 1, "MIN": 0, "MAX": 1},
		{"NAME": "amp", "LABEL": "Vibration", "TYPE": "float", "DEFAULT": 0.06, "MIN": 0, "MAX": 1},
		{"NAME": "width", "LABEL": "Width", "TYPE": "float", "DEFAULT": 0.0012, "MIN": 0.0002, "MAX": 0.05},
		{"NAME": "glow", "LABEL": "Glow", "TYPE": "float", "DEFAULT": 0.25, "MIN": 0, "MAX": 3},
		{"NAME": "count", "LABEL": "Threads", "TYPE": "float", "DEFAULT": 1, "MIN": 1, "MAX": 40},
		{"NAME": "spread", "LABEL": "Spread", "TYPE": "float", "DEFAULT": 0.02, "MIN": 0, "MAX": 0.2}
	]
}*/
void main() {
	vec2 uv = isf_FragNormCoord;
	float aspect = RENDERSIZE.x / RENDERSIZE.y;
	float away = abs(uv.y - 0.5) * 2.0;
	float ends = smoothstep(reach, reach - 0.03, away);
	float sum = 0.0;
	int n = int(clamp(count, 1.0, 40.0));
	for (int k = 0; k < n; ++k) {
		float f = float(k) - float(n - 1) * 0.5;
		float w = IMG_NORM_PIXEL(wave, vec2(fract(uv.y + float(k) * 0.077), 0.25)).r - 0.5;
		float at = x + f * spread + w * amp * (1.0 + abs(f) * 0.15);
		float d = abs(uv.x - at) * aspect;
		sum += smoothstep(width * 1.6, width * 0.4, d) + glow * 0.012 / (d + 0.012) * 0.12;
	}
	gl_FragColor = vec4(vec3(clamp(sum * ends, 0.0, 1.0)), clamp(sum * ends, 0.0, 1.0));
}
