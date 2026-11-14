/*{
    "ISFVSN": "2",
    "DESCRIPTION": "ACES filmic curve — cinematic highlight compression. Adapted from Ghost Arcade's Filmic Tonemap effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Color",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "curveValue",
            "LABEL": "Curve",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "exposure",
            "LABEL": "Exposure",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "contrastValue",
            "LABEL": "Contrast",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "mixValue",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (filmicTonemapHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uCurve (curveValue)
#define uExposure (exposure)
#define uContrast (contrastValue)
#define uMix (mixValue)

       // 0=ACES, 1=Reinhard, 2=Hable, 3=Bleach Bypass, 4=Print Film, 5=Soft Clip
    // 0.25-4.0 — pre-tonemap gain
    // 0-1 — post-tonemap S-curve

// ACES Narkowicz approximation.
vec3 aces(vec3 x) {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
vec3 reinhard(vec3 x) { return x / (1.0 + x); }
vec3 hable(vec3 x) {
  float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30, W = 11.2;
  vec3 n = ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
  float wn = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
  return n / wn;
}

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 src = texColor.rgb * uExposure;
  int mode = int(uCurve + 0.5);
  vec3 mapped;

  if (mode == 0)      mapped = aces(src);
  else if (mode == 1) mapped = reinhard(src);
  else if (mode == 2) mapped = hable(src);
  else if (mode == 3) {
    // Bleach Bypass — desaturated / high-contrast film look.
    vec3 lum = vec3(dot(src, vec3(0.299, 0.587, 0.114)));
    mapped = clamp(mix(src, lum * 1.4, 0.5), 0.0, 1.0);
    mapped = aces(mapped);
  }
  else if (mode == 4) {
    // Print Film — soft toe, gentle shoulder, slightly cooled.
    mapped = aces(src * vec3(0.95, 0.97, 1.05));
    mapped = pow(mapped, vec3(1.0 / 1.1));
  }
  else {
    // Soft Clip — Reinhard with a sharper knee.
    mapped = src / (1.0 + src * 0.5);
  }

  // Optional post S-curve for extra punch.
  if (uContrast > 0.001) {
    vec3 t = smoothstep(0.0, 1.0, mapped);
    mapped = mix(mapped, t * t * (3.0 - 2.0 * t), uContrast);
  }

  gl_FragColor = vec4(mix(texColor.rgb, mapped, uMix), texColor.a);
}
