/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Phase-shifted ghost copies with chromatic shift — spirit photography look. Adapted from Ghost Arcade's Ghost Double effect.",
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
            "NAME": "opacity",
            "LABEL": "Opacity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "offsetX",
            "LABEL": "Offset X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.05
        },
        {
            "NAME": "offsetY",
            "LABEL": "Offset Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "mirrorValue",
            "LABEL": "Mirror",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
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
        },
        {
            "NAME": "blend",
            "LABEL": "Blend",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (ghostDoubleHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uOpacity (opacity)
#define uOffsetX (offsetX)
#define uOffsetY (offsetY)
#define uMirror (mirrorValue)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uBlend (blend)

       // 0-1
       // -0.3..0.3
       // -0.3..0.3
        // 0/1 (mirror the ghost)



         // 0=screen, 1=add, 2=multiply

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec2 ghostUv = vUv - vec2(uOffsetX, uOffsetY);
  if (uMirror > 0.5) ghostUv.x = 1.0 - ghostUv.x;
  ghostUv = clamp(ghostUv, vec2(0.0), vec2(1.0));
  vec3 ghost = IMG_NORM_PIXEL(inputImage, ghostUv).rgb * vec3(uTintR, uTintG, uTintB) * uOpacity;

  int mode = int(uBlend + 0.5);
  vec3 result;
  if (mode == 0) result = 1.0 - (1.0 - src) * (1.0 - ghost);
  else if (mode == 1) result = src + ghost;
  else result = src * (1.0 + ghost);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
