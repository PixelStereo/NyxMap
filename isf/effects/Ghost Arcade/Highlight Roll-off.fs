/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Soft-clip highlights with cinematic shoulder. Adapted from Ghost Arcade's Highlight Roll-off effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Color",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "amountValue",
            "LABEL": "Amount",
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
            "NAME": "softness",
            "LABEL": "Softness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "preserveHue",
            "LABEL": "Preserve Hue",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "maxValue",
            "LABEL": "Max Value",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
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

// Ported from Ghost Arcade (highlightRolloffHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amountValue)
#define uThreshold (thresholdValue)
#define uSoftness (softness)
#define uPreserveHue (preserveHue)
#define uMaxValue (maxValue)
#define uMix (mixValue)

        // 0-1 rolloff strength
     // 0-1 where rolloff begins
      // 0-1
   // 0-1 preserve hue while rolling off
      // 0.7-1.5 ceiling for compressed highlights
           // 0-1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 hueAwareRolloff(vec3 src, float threshold, float maxV, float amount) {
  float l = luma(src);
  // Rolloff curve: hyperbolic compress
  float over = max(0.0, l - threshold);
  float compressed = threshold + over / (1.0 + over * (4.0 * amount));
  compressed = min(compressed, maxV);
  float scale = (l > 0.001) ? compressed / l : 1.0;
  return src * scale;
}

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  float l = luma(src.rgb);
  float w = smoothstep(uThreshold - uSoftness, uThreshold + uSoftness * 0.5 + 0.001, l);

  vec3 rolledHue = hueAwareRolloff(src.rgb, uThreshold, uMaxValue, uAmount);
  vec3 rolledRgb = vec3(
    min(src.r, mix(src.r, uThreshold + (src.r - uThreshold) / (1.0 + (src.r - uThreshold) * 4.0 * uAmount), w)),
    min(src.g, mix(src.g, uThreshold + (src.g - uThreshold) / (1.0 + (src.g - uThreshold) * 4.0 * uAmount), w)),
    min(src.b, mix(src.b, uThreshold + (src.b - uThreshold) / (1.0 + (src.b - uThreshold) * 4.0 * uAmount), w))
  );
  vec3 result = mix(rolledRgb, rolledHue, uPreserveHue);

  gl_FragColor = vec4(mix(src.rgb, result, uMix * w), src.a);
}
