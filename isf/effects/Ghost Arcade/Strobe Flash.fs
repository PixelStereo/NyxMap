/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Rhythmic flash/pulse — white, black, invert, or color strobe modes. Adapted from Ghost Arcade's Strobe Flash effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Trails",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "rate",
            "LABEL": "Rate",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "duty",
            "LABEL": "Duty",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
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
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (strobeFlashHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uRate (rate)
#define uDuty (duty)
#define uIntensity (intensity)
#define uMode float(floor(modeValue + 0.5))
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)

          // 0.5-30 Hz
           // 0-1 fraction of cycle bright
     // 0-2
          // 0=on/off, 1=invert flash, 2=tint flash

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  float phase = mod(uTime * uRate, 1.0);
  float gate = step(phase, uDuty);

  int mode = int(uMode + 0.5);
  vec3 result;
  if (mode == 0) {
    result = mix(src, src + vec3(uIntensity * gate), gate);
  } else if (mode == 1) {
    result = mix(src, 1.0 - src, gate * uIntensity);
  } else {
    vec3 tint = vec3(uTintR, uTintG, uTintB);
    result = mix(src, src * tint + tint * 0.4, gate * uIntensity);
  }
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
