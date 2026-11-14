/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Gaussian blur. Adapted from Ghost Arcade's Blur effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Blur & Focus",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "blurRadius",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 30.0,
            "DEFAULT": 5.0
        },
        {
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "quality",
            "LABEL": "Quality",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "edgeProtect",
            "LABEL": "Edge Protect",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
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

// Ported from Ghost Arcade (blurHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uRadius (blurRadius)
#define uMode float(floor(modeValue + 0.5))
#define uAngle (angle)
#define uQuality (quality)
#define uEdgeProtect (edgeProtect)
#define uMix (mixValue)

        // 0-30 px
          // 0=box, 1=gaussian, 2=motion, 3=bilateral
         // 0-360 degrees (motion blur direction)
       // 0=low (9-tap), 1=mid (17-tap), 2=high (25-tap)
   // 0-1 bilateral edge preservation
           // 0-1 wet/dry

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 srcRgb = src.rgb;
  float srcL = luma(srcRgb);
  if (uRadius < 0.01) {
    gl_FragColor = src;
    return;
  }

  int mode = int(uMode + 0.5);
  int taps = (uQuality < 0.5) ? 4 : (uQuality < 1.5) ? 8 : 12;
  float r = uRadius;
  vec2 texel = 1.0 / uResolution;

  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  float ang = radians(uAngle);
  vec2 motionDir = vec2(cos(ang), sin(ang));

  for (int s = -12; s <= 12; s++) {
    if (abs(s) > taps) continue;
    float fs = float(s);
    vec2 off;
    float w = 1.0;

    if (mode == 0) {
      // Box blur — 1D pair (do twice)
      off = vec2(fs, 0.0) * texel * (r / float(taps));
      w = 1.0;
    } else if (mode == 1) {
      // Gaussian (separated horizontal-only here, ok for screen-space)
      off = vec2(fs, 0.0) * texel * (r / float(taps));
      float sigma = float(taps) * 0.5;
      w = exp(-(fs * fs) / (2.0 * sigma * sigma));
    } else if (mode == 2) {
      // Motion blur — directional
      off = motionDir * fs * texel * (r / float(taps));
      w = 1.0;
    } else {
      // Bilateral
      off = vec2(fs, 0.0) * texel * (r / float(taps));
      vec3 sCol = IMG_NORM_PIXEL(inputImage, vUv + off).rgb;
      float dL = luma(sCol) - srcL;
      float spatial = exp(-(fs * fs) / (2.0 * float(taps * taps) * 0.25));
      float range = exp(-(dL * dL) / (2.0 * pow(0.1 + (1.0 - uEdgeProtect) * 0.5, 2.0)));
      w = spatial * range;
    }
    acc += IMG_NORM_PIXEL(inputImage, vUv + off).rgb * w;
    wsum += w;
  }

  // Second pass for box/gaussian/bilateral (vertical), so output_ is roughly 2D
  if (mode != 2) {
    for (int s = -12; s <= 12; s++) {
      if (abs(s) > taps) continue;
      if (s == 0) continue;
      float fs = float(s);
      vec2 off = vec2(0.0, fs) * texel * (r / float(taps));
      float w = 1.0;
      if (mode == 1) {
        float sigma = float(taps) * 0.5;
        w = exp(-(fs * fs) / (2.0 * sigma * sigma));
      } else if (mode == 3) {
        vec3 sCol = IMG_NORM_PIXEL(inputImage, vUv + off).rgb;
        float dL = luma(sCol) - srcL;
        float spatial = exp(-(fs * fs) / (2.0 * float(taps * taps) * 0.25));
        float range = exp(-(dL * dL) / (2.0 * pow(0.1 + (1.0 - uEdgeProtect) * 0.5, 2.0)));
        w = spatial * range;
      }
      acc += IMG_NORM_PIXEL(inputImage, vUv + off).rgb * w;
      wsum += w;
    }
  }

  vec3 blurred = acc / max(wsum, 0.0001);
  vec3 result = mix(srcRgb, blurred, uMix);
  gl_FragColor = vec4(result, src.a);
}
