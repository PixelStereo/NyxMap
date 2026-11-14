/*{
    "ISFVSN": "2",
    "DESCRIPTION": "6-point star diffraction spikes on bright areas. Adapted from Ghost Arcade's Glint / Starburst effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Atmosphere",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "threshold",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.5,
            "DEFAULT": 0.75
        },
        {
            "NAME": "lengthValue",
            "LABEL": "Length",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "pointsValue",
            "LABEL": "Points",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "rotation",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "colorR",
            "LABEL": "Color R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "colorG",
            "LABEL": "Color G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.9,
            "DEFAULT": 0.95
        },
        {
            "NAME": "colorB",
            "LABEL": "Color B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
        }
    ]
}*/

// Ported from Ghost Arcade (glintStarburstHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (intensity)
#define uThreshold (threshold)
#define uLength (lengthValue)
#define uPoints (pointsValue)
#define uRotation (rotation)
#define uColorR (colorR)
#define uColorG (colorG)
#define uColorB (colorB)

     // 0-2
     // 0-1
        // 0-1
        // 4-12 star points
      // 0-360

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uIntensity < 0.001) { gl_FragColor = src; return; }

  int points = int(clamp(uPoints, 2.0, 12.0)) * 2;
  float maxLen = uLength * 0.15;
  vec2 texel = 1.0 / uResolution;

  vec3 burst = vec3(0.0);
  for (int i = 0; i < 24; i++) {
    if (i >= points) break;
    float ang = radians(uRotation) + float(i) * 6.28318 / float(points);
    vec2 dir = vec2(cos(ang), sin(ang));
    // March along ray
    for (int s = 1; s <= 12; s++) {
      float t = float(s) / 12.0;
      vec2 sp = vUv + dir * maxLen * t;
      vec3 sc = IMG_NORM_PIXEL(inputImage, sp).rgb;
      float gate = smoothstep(uThreshold, uThreshold + 0.15, luma(sc));
      burst += sc * gate * (1.0 - t) * (1.0 - t);
    }
  }
  burst /= float(points);
  burst *= uIntensity * vec3(uColorR, uColorG, uColorB);

  vec3 result = 1.0 - (1.0 - src.rgb) * (1.0 - burst);
  gl_FragColor = vec4(result, src.a);
}
