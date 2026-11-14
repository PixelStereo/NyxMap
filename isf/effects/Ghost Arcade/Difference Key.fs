/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Key based on color difference from reference. Adapted from Ghost Arcade's Difference Key effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Keying",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "refR",
            "LABEL": "Ref R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "refG",
            "LABEL": "Ref G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "refB",
            "LABEL": "Ref B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "tolerance",
            "LABEL": "Tolerance",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "softness",
            "LABEL": "Softness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.15
        },
        {
            "NAME": "invert",
            "LABEL": "Invert",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "matteValue",
            "LABEL": "Matte",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (differenceKeyHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uRefR (refR)
#define uRefG (refG)
#define uRefB (refB)
#define uTolerance (tolerance)
#define uSoftness (softness)
#define uInvert (invert)
#define uMatte (matteValue)
#define uMode float(floor(modeValue + 0.5))

          // 0-1 reference colour


     // 0-1
      // 0-1
        // 0=key matches, 1=key non-matches
         // 0=normal, 1=show matte
          // 0=Euclidean, 1=Manhattan, 2=Max channel

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec3 ref = vec3(uRefR, uRefG, uRefB);
  vec3 diff = abs(src - ref);

  int mode = int(uMode + 0.5);
  float d;
  if (mode == 0) d = length(diff);
  else if (mode == 1) d = (diff.r + diff.g + diff.b);
  else d = max(max(diff.r, diff.g), diff.b);

  float matte = smoothstep(uTolerance, uTolerance + uSoftness + 0.001, d);
  if (uInvert > 0.5) matte = 1.0 - matte;

  if (uMatte > 0.5) {
    gl_FragColor = vec4(vec3(matte), 1.0);
  } else {
    gl_FragColor = vec4(src, matte);
  }
}
