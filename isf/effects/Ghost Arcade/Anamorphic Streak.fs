/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Horizontal light streaks like anamorphic lenses. Adapted from Ghost Arcade's Anamorphic Streak effect.",
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
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "lengthValue",
            "LABEL": "Length",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "thresholdValue",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.5,
            "DEFAULT": 0.75
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
            "NAME": "angleValue",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "samplesValue",
            "LABEL": "Samples",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 64.0,
            "DEFAULT": 32.0
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

// Ported from Ghost Arcade (anamorphicStreakHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (intensity)
#define uLength (lengthValue)
#define uThreshold (thresholdValue)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uAngle (angleValue)
#define uSamples (samplesValue)
#define uMix (mixValue)

     // 0-2
        // 0-1 streak length (% of screen)
     // 0-1 highlight threshold
         // 0-1 streak colour (typically blue)
         // 0-1
         // 0-1
         // 0-180 streak angle (default horizontal)
       // 16-64
           // 0-1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uIntensity < 0.001) { gl_FragColor = src; return; }

  int samples = int(clamp(uSamples, 8.0, 64.0));
  float ang = radians(uAngle);
  vec2 dir = vec2(cos(ang), sin(ang));
  vec2 texel = 1.0 / uResolution;

  vec3 streak = vec3(0.0);
  float wsum = 0.0;
  for (int i = -64; i <= 64; i++) {
    if (abs(i) > samples) continue;
    float t = float(i) / float(samples);
    vec2 off = dir * t * uLength;
    vec3 sCol = IMG_NORM_PIXEL(inputImage, vUv + off).rgb;
    float gate = smoothstep(uThreshold, uThreshold + 0.15, luma(sCol));
    float w = exp(-abs(t) * 2.0);
    streak += sCol * gate * w;
    wsum += w;
  }
  streak = (wsum > 0.0) ? streak / wsum : vec3(0.0);
  streak *= vec3(uTintR, uTintG, uTintB) * uIntensity;

  // Screen blend
  vec3 result = 1.0 - (1.0 - src.rgb) * (1.0 - streak);
  gl_FragColor = vec4(mix(src.rgb, result, uMix), src.a);
}
