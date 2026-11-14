/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Bilateral ink symmetry with animated fluid edges. Vertical / horizontal / both / 4-fold mirror modes. Adapted from Ghost Arcade's Rorschach Mirror effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "New Hero",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "inkAmount",
            "LABEL": "Ink Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "fluidEdges",
            "LABEL": "Fluid Edges",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.05
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.05
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.05
        },
        {
            "NAME": "bgR",
            "LABEL": "Bg R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.9,
            "DEFAULT": 0.95
        },
        {
            "NAME": "bgG",
            "LABEL": "Bg G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.9,
            "DEFAULT": 0.95
        },
        {
            "NAME": "bgB",
            "LABEL": "Bg B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.84,
            "DEFAULT": 0.92
        },
        {
            "NAME": "mixOriginal",
            "LABEL": "Mix Original",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (rorschachMirrorHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(modeValue + 0.5))
#define uInkAmount (inkAmount)
#define uFluidEdges (fluidEdges)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uBgR (bgR)
#define uBgG (bgG)
#define uBgB (bgB)
#define uMixOriginal (mixOriginal)

          // 0=vertical, 1=horizontal, 2=both, 3=4-fold
     // 0-1 contrast/threshold for ink
    // 0-1 animated noise on mirror seam






   // 0-1

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  float a = hash21(i), b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  int mode = int(uMode + 0.5);
  vec2 sUv = vUv;
  // Animated edge wobble at fold seam
  float wobble = uFluidEdges * (vnoise(vUv * 8.0 + uTime * 0.5) - 0.5) * 0.04;

  if (mode == 0 || mode == 2 || mode == 3) {
    // Vertical fold (mirror left↔right)
    if (sUv.x > 0.5 + wobble) sUv.x = 1.0 - sUv.x;
  }
  if (mode == 1 || mode == 2 || mode == 3) {
    if (sUv.y > 0.5 + wobble) sUv.y = 1.0 - sUv.y;
  }
  if (mode == 3) {
    // 4-fold: also diagonal mirror
    if (sUv.x > sUv.y) {
      float tmp = sUv.x; sUv.x = sUv.y; sUv.y = tmp;
    }
  }
  sUv = clamp(sUv, vec2(0.0), vec2(1.0));
  vec3 mirrored = IMG_NORM_PIXEL(inputImage, sUv).rgb;
  float ink = smoothstep(uInkAmount, 1.0, luma(mirrored));
  vec3 inkColor = mix(vec3(uBgR, uBgG, uBgB), vec3(uTintR, uTintG, uTintB), ink);

  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec3 result = mix(inkColor, src, uMixOriginal);
  gl_FragColor = vec4(result, 1.0);
}
