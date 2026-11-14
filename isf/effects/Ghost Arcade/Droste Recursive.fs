/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Recursive spiral zoom — Escher-style infinite mirror. Adapted from Ghost Arcade's Droste Recursive effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Warp",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "zoomValue",
            "LABEL": "Zoom",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.5
        },
        {
            "NAME": "rotationValue",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 10.0,
            "DEFAULT": 5.0
        },
        {
            "NAME": "iterations",
            "LABEL": "Iterations",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 12.0,
            "DEFAULT": 6.0
        },
        {
            "NAME": "offsetX",
            "LABEL": "Offset X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "offsetY",
            "LABEL": "Offset Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "frameSize",
            "LABEL": "Frame Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
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

// Ported from Ghost Arcade (drosteHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uZoom (zoomValue)
#define uRotation (rotationValue)
#define uIterations (iterations)
#define uOffsetX (offsetX)
#define uOffsetY (offsetY)
#define uFrameSize (frameSize)
#define uMix (mixValue)

          // 1.05-3 per-iteration zoom
      // 0-360 per-iteration rotation
    // 1-12
       // 0-1
       // 0-1
     // 0-0.5 mask region as fraction of screen
           // 0-1

void main() {
  vec2 c = vec2(uOffsetX, uOffsetY);
  int iters = int(clamp(uIterations, 1.0, 12.0));
  vec2 uv = vUv;
  float ang = radians(uRotation);

  // Iteratively zoom toward c by uZoom each time, until uv falls in frame mask
  for (int i = 0; i < 12; i++) {
    if (i >= iters) break;
    vec2 d = uv - c;
    float r = length(d - 0.5 + c);
    // If outside frame band, zoom in further
    if (r > uFrameSize) {
      d *= uZoom;
      // Rotate
      float ca = cos(ang), sa = sin(ang);
      d = vec2(d.x * ca - d.y * sa, d.x * sa + d.y * ca);
      uv = c + d;
    }
  }
  uv = clamp(uv, vec2(0.0), vec2(1.0));
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  vec4 droste = IMG_NORM_PIXEL(inputImage, uv);
  gl_FragColor = vec4(mix(src.rgb, droste.rgb, uMix), src.a);
}
