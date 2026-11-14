/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Directional paint smear with moveable line position. Horizontal, vertical, radial, or rotational modes. Adapted from Ghost Arcade's Time Smear effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Distort",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "mode",
            "LABEL": "Direction",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "amount",
            "LABEL": "Line Position",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Smear Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (timeSmearShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode (mode)
#define uAmount (amount)
#define uAmount2 (amount2)
#define uSpeed (speed)

void main() {
  int mode = int(uMode + 0.5);
  vec2 uv = vUv;
  float intensity = uAmount2;
  float t = uTime * uSpeed;

  vec4 color = vec4(0.0);

  if (mode == 0) {
    float linePos = uAmount;
    float dist = uv.x - linePos;
    float strength = dist * intensity;
    for (int i = 0; i < 16; i++) {
      float fi = float(i) / 15.0;
      vec2 offset = vec2(strength * fi * sin(t * 0.3 + fi), 0.0);
      color += IMG_NORM_PIXEL(inputImage, clamp(uv - offset, 0.0, 1.0));
    }
    color /= 16.0;
  }
  else if (mode == 1) {
    float linePos = uAmount;
    float dist = uv.y - linePos;
    float strength = dist * intensity;
    for (int i = 0; i < 16; i++) {
      float fi = float(i) / 15.0;
      vec2 offset = vec2(0.0, strength * fi * sin(t * 0.3 + fi));
      color += IMG_NORM_PIXEL(inputImage, clamp(uv - offset, 0.0, 1.0));
    }
    color /= 16.0;
  }
  else if (mode == 2) {
    vec2 center = vec2(uAmount, 0.5);
    vec2 dir = uv - center;
    float dist = length(dir);
    float strength = dist * intensity;
    for (int i = 0; i < 16; i++) {
      float fi = float(i) / 15.0;
      vec2 offset = normalize(dir + vec2(0.001)) * strength * fi;
      color += IMG_NORM_PIXEL(inputImage, clamp(uv - offset, 0.0, 1.0));
    }
    color /= 16.0;
  }
  else {
    vec2 center = vec2(0.5);
    vec2 d = uv - center;
    float angle = atan(d.y, d.x);
    float dist = length(d);
    float strength = intensity * 0.3;
    for (int i = 0; i < 16; i++) {
      float fi = float(i) / 15.0 - 0.5;
      float a = angle + fi * strength;
      vec2 p = center + vec2(cos(a), sin(a)) * dist;
      color += IMG_NORM_PIXEL(inputImage, clamp(p, 0.0, 1.0));
    }
    color /= 16.0;
  }

  gl_FragColor = color;
}
