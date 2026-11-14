/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Light painting — bright pixels leave luminance trails along flow paths. Adapted from Ghost Arcade's Light Paint effect.",
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
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "threshold",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "trailLength",
            "LABEL": "Trail Length",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "flowAngle",
            "LABEL": "Flow Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "flowScale",
            "LABEL": "Flow Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 12.0,
            "DEFAULT": 6.0
        },
        {
            "NAME": "chromaShift",
            "LABEL": "Chroma Shift",
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
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.6,
            "DEFAULT": 0.8
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        }
    ]
}*/

// Ported from Ghost Arcade (lightPaintHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (intensity)
#define uThreshold (threshold)
#define uTrailLength (trailLength)
#define uFlowAngle (flowAngle)
#define uFlowScale (flowScale)
#define uChromaShift (chromaShift)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)

     // 0-2
     // 0-1 luma gate
   // 0-1
     // 0-360 dominant flow direction
     // 1-16 noise warp scale
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
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  if (uIntensity < 0.001) { gl_FragColor = vec4(src, 1.0); return; }

  // Build flow direction at this pixel (perturbed by noise)
  float baseAng = radians(uFlowAngle);
  float perturb = (vnoise(vUv * uFlowScale + uTime * 0.3) - 0.5) * 1.6;
  float ang = baseAng + perturb;
  vec2 dir = vec2(cos(ang), sin(ang));

  // March backward along flow accumulating bright pixels
  vec3 acc = src;
  float wsum = 1.0;
  float maxLen = uTrailLength * 0.4;
  for (int i = 1; i <= 24; i++) {
    float t = float(i) / 24.0;
    vec2 sUv = vUv - dir * maxLen * t;
    vec3 sCol;
    if (uChromaShift > 0.001) {
      sCol.r = IMG_NORM_PIXEL(inputImage, sUv + dir * t * uChromaShift * 0.02).r;
      sCol.g = IMG_NORM_PIXEL(inputImage, sUv).g;
      sCol.b = IMG_NORM_PIXEL(inputImage, sUv - dir * t * uChromaShift * 0.02).b;
    } else {
      sCol = IMG_NORM_PIXEL(inputImage, sUv).rgb;
    }
    float gate = smoothstep(uThreshold, uThreshold + 0.15, luma(sCol));
    float w = (1.0 - t) * gate;
    acc += sCol * vec3(uTintR, uTintG, uTintB) * w * uIntensity;
    wsum += w;
  }
  vec3 result = acc / wsum;
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
