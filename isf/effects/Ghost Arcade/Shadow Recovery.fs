/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Lift shadow detail without blowing highlights. Adapted from Ghost Arcade's Shadow Recovery effect.",
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
            "NAME": "amount",
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
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "softnessValue",
            "LABEL": "Softness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "colorRecovery",
            "LABEL": "Color Recovery",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "highlightProtect",
            "LABEL": "Highlight Protect",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
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

// Ported from Ghost Arcade (shadowRecoveryHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uThreshold (thresholdValue)
#define uSoftness (softnessValue)
#define uColorRecovery (colorRecovery)
#define uHighlightProtect (highlightProtect)
#define uMix (mixValue)

        // 0-1 shadow lift
     // 0-1 shadow zone
      // 0-1 transition softness
 // 0-1 boost saturation in shadows
 // 0-1
           // 0-1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  float l = luma(src.rgb);
  // Shadow weight: 1 at black, 0 above threshold (with softness)
  float w = 1.0 - smoothstep(uThreshold, uThreshold + uSoftness + 0.001, l);

  // Lift formula: pull shadows toward midtone using a power curve
  float liftPow = mix(1.0, 0.45, uAmount);
  vec3 lifted = pow(max(src.rgb, 0.0001), vec3(liftPow));

  // Highlight protect — fade lift back near 1.0
  float highW = smoothstep(0.7, 1.0, l);
  float effW = w * (1.0 - highW * uHighlightProtect);
  vec3 result = mix(src.rgb, lifted, effW);

  // Optional color recovery (boost saturation in lifted shadows)
  if (uColorRecovery > 0.001) {
    float rl = luma(result);
    vec3 boosted = mix(vec3(rl), result, 1.0 + uColorRecovery * 0.6);
    result = mix(result, boosted, effW);
  }

  gl_FragColor = vec4(mix(src.rgb, result, uMix), src.a);
}
