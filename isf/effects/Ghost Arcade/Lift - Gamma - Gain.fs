/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Three-way color grading for shadows, mids, highlights. Adapted from Ghost Arcade's Lift / Gamma / Gain effect.",
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
            "NAME": "liftR",
            "LABEL": "Lift R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "liftG",
            "LABEL": "Lift G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "liftB",
            "LABEL": "Lift B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "gammaR",
            "LABEL": "Gamma R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "gammaG",
            "LABEL": "Gamma G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "gammaB",
            "LABEL": "Gamma B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "gainR",
            "LABEL": "Gain R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "gainG",
            "LABEL": "Gain G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "gainB",
            "LABEL": "Gain B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "lumaOnly",
            "LABEL": "Luma Only",
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

// Ported from Ghost Arcade (liftGammaGainHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uLiftR (liftR)
#define uLiftG (liftG)
#define uLiftB (liftB)
#define uGammaR (gammaR)
#define uGammaG (gammaG)
#define uGammaB (gammaB)
#define uGainR (gainR)
#define uGainG (gainG)
#define uGainB (gainB)
#define uLumaOnly (lumaOnly)
#define uMix (mixValue)

         // -0.5..+0.5
      // 0.5..1.5 (1 = neutral)
         // 0.5..2.0
// 0-1 — bypass color shifts, apply intensity only

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 src = texColor.rgb;

  vec3 lift  = vec3(uLiftR,  uLiftG,  uLiftB);
  vec3 gamma = vec3(uGammaR, uGammaG, uGammaB);
  vec3 gain  = vec3(uGainR,  uGainG,  uGainB);

  if (uLumaOnly > 0.5) {
    // Average the channels for luma-only operation.
    float l = (lift.r + lift.g + lift.b) / 3.0;
    float g = (gamma.r + gamma.g + gamma.b) / 3.0;
    float gn = (gain.r + gain.g + gain.b) / 3.0;
    lift = vec3(l); gamma = vec3(g); gain = vec3(gn);
  }

  // Standard ASC-CDL-ish formula: out = pow((src - 0) * gain + lift * (1 - src), 1/gamma)
  // Simplified for our knob ranges — lift adds in shadows (multiplied by
  // 1-src so it doesn't blow out highlights), gain multiplies, gamma is
  // the inverse exponent.
  vec3 lifted = src + lift * (vec3(1.0) - src) * 0.5;
  vec3 gained = lifted * gain;
  vec3 graded = pow(max(gained, vec3(0.0)), vec3(1.0) / max(gamma, vec3(0.05)));

  gl_FragColor = vec4(clamp(mix(src, graded, uMix), 0.0, 1.0), texColor.a);
}
