/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Mirror image horizontally, vertically, or both. Adapted from Ghost Arcade's Mirror effect.",
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
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "positionValue",
            "LABEL": "Position",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "offsetValue",
            "LABEL": "Offset",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "flipSide",
            "LABEL": "Flip Side",
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

// Ported from Ghost Arcade (mirrorHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(modeValue + 0.5))
#define uPosition (positionValue)
#define uOffset (offsetValue)
#define uFlipSide (flipSide)
#define uMix (mixValue)

          // 0=horizontal, 1=vertical, 2=quad, 3=diagonal
      // 0-1 mirror axis position
        // 0-1 source offset
      // 0=mirror right/bottom, 1=mirror left/top
           // 0-1

void main() {
  vec2 uv = vUv;
  int mode = int(uMode + 0.5);

  if (mode == 0) {
    // Horizontal axis (mirror along Y)
    float pos = uPosition;
    bool aboveAxis = (uv.x > pos) != (uFlipSide > 0.5);
    if (aboveAxis) {
      uv.x = pos * 2.0 - uv.x + (uOffset - 0.5) * 0.4;
    }
  } else if (mode == 1) {
    // Vertical axis (mirror along X)
    float pos = uPosition;
    bool aboveAxis = (uv.y > pos) != (uFlipSide > 0.5);
    if (aboveAxis) {
      uv.y = pos * 2.0 - uv.y + (uOffset - 0.5) * 0.4;
    }
  } else if (mode == 2) {
    // Quad mirror
    uv = abs(uv - 0.5) + 0.5;
    uv = abs(uv - vec2(uPosition, uPosition));
    uv = mix(uv, vUv, 0.0);
  } else {
    // Diagonal
    vec2 c = vec2(uPosition);
    vec2 d = uv - c;
    // Reflect across diagonal y=x going through center
    vec2 refl = c + vec2(d.y, d.x);
    uv = (uFlipSide > 0.5) ? refl : (uv.x + uv.y < 2.0 * uPosition ? uv : refl);
  }

  uv = clamp(uv, vec2(0.0), vec2(1.0));
  vec4 mirrored = IMG_NORM_PIXEL(inputImage, uv);
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(mix(src.rgb, mirrored.rgb, uMix), src.a);
}
