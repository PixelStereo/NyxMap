/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Radial zoom blur from center point. Adapted from Ghost Arcade's Zoom Blur effect.",
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
            "NAME": "amount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.25
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
            "NAME": "samplesValue",
            "LABEL": "Samples",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 32.0,
            "DEFAULT": 16.0
        },
        {
            "NAME": "falloffValue",
            "LABEL": "Falloff",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
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

// Ported from Ghost Arcade (zoomBlurHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uSamples (samplesValue)
#define uFalloff (falloffValue)
#define uChromatic (chromatic)
#define uMix (mixValue)

        // 0-1 normalized blur length
       // 0-1
       // 0-1
       // 4-32
       // 0-1 weight falloff
     // 0-1 RGB split during zoom
           // 0-1 wet/dry

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uAmount < 0.001) { gl_FragColor = src; return; }

  int samples = int(clamp(uSamples, 2.0, 32.0));
  vec2 center = vec2(uCenterX, uCenterY);
  vec2 dir = vUv - center;

  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  for (int i = 0; i <= 32; i++) {
    if (i > samples) break;
    float t = float(i) / float(samples);
    vec2 sUv;
    vec3 c;
    if (uChromatic > 0.001) {
      // Channel-shift along zoom direction
      float rT = t * (1.0 + uChromatic * 0.05);
      float gT = t;
      float bT = t * (1.0 - uChromatic * 0.05);
      float rR = IMG_NORM_PIXEL(inputImage, vUv - dir * uAmount * rT).r;
      float gG = IMG_NORM_PIXEL(inputImage, vUv - dir * uAmount * gT).g;
      float bB = IMG_NORM_PIXEL(inputImage, vUv - dir * uAmount * bT).b;
      c = vec3(rR, gG, bB);
    } else {
      sUv = vUv - dir * uAmount * t;
      c = IMG_NORM_PIXEL(inputImage, sUv).rgb;
    }
    float w = mix(1.0, 1.0 - t, uFalloff);
    acc += c * w;
    wsum += w;
  }
  vec3 blurred = acc / wsum;
  gl_FragColor = vec4(mix(src.rgb, blurred, uMix), src.a);
}
