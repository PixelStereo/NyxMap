/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Volumetric light rays from bright source. Adapted from Ghost Arcade's God Rays effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Light & Glow",
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
            "NAME": "decayValue",
            "LABEL": "Decay",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.9,
            "DEFAULT": 0.95
        },
        {
            "NAME": "exposureValue",
            "LABEL": "Exposure",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "densityValue",
            "LABEL": "Density",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.9,
            "DEFAULT": 0.95
        },
        {
            "NAME": "threshold",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
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
            "DEFAULT": 0.2
        },
        {
            "NAME": "samplesValue",
            "LABEL": "Samples",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 128.0,
            "DEFAULT": 64.0
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
            "MAX": 1.9,
            "DEFAULT": 0.95
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
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

// Ported from Ghost Arcade (godRaysHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (intensity)
#define uDecay (decayValue)
#define uExposure (exposureValue)
#define uDensity (densityValue)
#define uThreshold (threshold)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uSamples (samplesValue)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uMix (mixValue)

     // 0-2
         // 0.85-1.0 sample_ decay
      // 0.1-1 exposure scale
       // 0-1 sample_ density
     // 0-1 brightness gate
       // 0-1 sun position
       // 0-1
       // 16-128
         // 0-1
         // 0-1
         // 0-1
           // 0-1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uIntensity < 0.001) { gl_FragColor = src; return; }

  int samples = int(clamp(uSamples, 8.0, 128.0));
  vec2 sun = vec2(uCenterX, uCenterY);
  vec2 deltaUv = (vUv - sun) * (uDensity / float(samples));
  vec2 cur = vUv;
  float illum = 1.0;
  vec3 acc = vec3(0.0);

  for (int i = 0; i < 128; i++) {
    if (i >= samples) break;
    cur -= deltaUv;
    vec3 s = IMG_NORM_PIXEL(inputImage, cur).rgb;
    // Threshold gate — only bright pixels emit rays
    float gate = smoothstep(uThreshold, uThreshold + 0.15, luma(s));
    acc += s * gate * illum;
    illum *= uDecay;
  }
  acc *= uExposure * uIntensity;
  acc *= vec3(uTintR, uTintG, uTintB);

  vec3 result = src.rgb + acc * uMix;
  gl_FragColor = vec4(result, src.a);
}
