/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Cartesian to polar coordinate warp — wrap to circle. Adapted from Ghost Arcade's Polar Transform effect.",
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
            "NAME": "rotation",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "zoom",
            "LABEL": "Zoom",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "centerX",
            "LABEL": "Center X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "centerY",
            "LABEL": "Center Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
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

// Ported from Ghost Arcade (polarTransformHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(modeValue + 0.5))
#define uRotation (rotation)
#define uZoom (zoom)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uMix (mixValue)

          // 0=cart→polar, 1=polar→cart, 2=log polar
      // 0-360
          // 0.25-4


           // 0-1

void main() {
  int mode = int(uMode + 0.5);
  vec2 c = vec2(uCenterX, uCenterY);
  vec2 sUv;

  if (mode == 0) {
    // Cart → polar (input_ becomes radial pattern)
    vec2 d = vUv - c;
    d.x *= uResolution.x / uResolution.y;
    float r = length(d) * 2.0;
    float a = atan(d.y, d.x) / 6.28318 + 0.5;
    a = fract(a + uRotation / 360.0);
    sUv = vec2(a, r * uZoom);
  } else if (mode == 1) {
    // Polar → cart (rectangular UV becomes radial)
    float a = (vUv.x - 0.5 + uRotation / 360.0) * 6.28318;
    float r = vUv.y * uZoom;
    vec2 d = vec2(cos(a), sin(a)) * r * 0.5;
    d.x *= uResolution.y / uResolution.x;
    sUv = c + d;
  } else {
    // Log polar
    vec2 d = vUv - c;
    d.x *= uResolution.x / uResolution.y;
    float r = log(length(d) * 2.0 + 1.0);
    float a = atan(d.y, d.x) / 6.28318 + 0.5;
    a = fract(a + uRotation / 360.0);
    sUv = vec2(a, r * uZoom);
  }

  sUv = clamp(sUv, vec2(0.0), vec2(1.0));
  vec4 mapped = IMG_NORM_PIXEL(inputImage, sUv);
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(mix(src.rgb, mapped.rgb, uMix), src.a);
}
