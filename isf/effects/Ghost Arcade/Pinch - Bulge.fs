/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Pinch inward or bulge outward from center. Adapted from Ghost Arcade's Pinch / Bulge effect.",
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
            "NAME": "amount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "radius",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
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
            "NAME": "falloff",
            "LABEL": "Falloff",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.5
        },
        {
            "NAME": "chromatic",
            "LABEL": "Chromatic",
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

// Ported from Ghost Arcade (pinchBulgeHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uRadius (radius)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uFalloff (falloff)
#define uChromatic (chromatic)
#define uMix (mixValue)

        // -1..1 negative=pinch, positive=bulge
        // 0.1-1
       // 0-1
       // 0-1
       // 0.5-4
     // 0-1 RGB split
           // 0-1

vec2 warp(vec2 uv, float amt) {
  vec2 c = vec2(uCenterX, uCenterY);
  vec2 d = uv - c;
  d.x *= uResolution.x / uResolution.y;
  float r = length(d);
  float maxR = max(0.001, uRadius);
  float t = clamp(r / maxR, 0.0, 1.0);
  float fade = pow(1.0 - t, max(0.5, uFalloff));
  float k = 1.0 + amt * fade;
  if (k < 0.001) k = 0.001;
  d /= k;
  d.x *= uResolution.y / uResolution.x;
  return c + d;
}

void main() {
  vec3 col;
  if (uChromatic > 0.001) {
    vec2 uvR = warp(vUv, uAmount * (1.0 + uChromatic * 0.05));
    vec2 uvG = warp(vUv, uAmount);
    vec2 uvB = warp(vUv, uAmount * (1.0 - uChromatic * 0.05));
    col.r = IMG_NORM_PIXEL(inputImage, clamp(uvR, vec2(0.0), vec2(1.0))).r;
    col.g = IMG_NORM_PIXEL(inputImage, clamp(uvG, vec2(0.0), vec2(1.0))).g;
    col.b = IMG_NORM_PIXEL(inputImage, clamp(uvB, vec2(0.0), vec2(1.0))).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, clamp(warp(vUv, uAmount), vec2(0.0), vec2(1.0))).rgb;
  }
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(mix(src.rgb, col, uMix), src.a);
}
