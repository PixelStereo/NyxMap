/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Dreamy pro-mist diffusion filter — soft highlight glow. Adapted from Ghost Arcade's Diffusion / Pro-Mist effect.",
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
            "NAME": "amount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "radiusValue",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
        },
        {
            "NAME": "thresholdValue",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "shadowLift",
            "LABEL": "Shadow Lift",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "highlightBloom",
            "LABEL": "Highlight Bloom",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "hazeValue",
            "LABEL": "Haze",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "hazeWarmth",
            "LABEL": "Haze Warmth",
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

// Ported from Ghost Arcade (diffusionPromistHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uRadius (radiusValue)
#define uThreshold (thresholdValue)
#define uShadowLift (shadowLift)
#define uHighlightBloom (highlightBloom)
#define uHaze (hazeValue)
#define uHazeWarmth (hazeWarmth)
#define uMix (mixValue)

        // 0-1 diffusion strength
        // 1-30 glow radius
     // 0-1 highlight threshold
    // 0-1 lift shadows
// 0-1 bloom on highlights
          // 0-1 overall haze (lower contrast)
    // 0-1 warm haze tint
           // 0-1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uAmount < 0.001) { gl_FragColor = src; return; }

  // Highlight-only blur for bloom
  vec2 texel = 1.0 / uResolution;
  vec3 bloom = vec3(0.0);
  float wsum = 0.0;
  float r = max(1.0, uRadius);
  for (int y = -5; y <= 5; y++) {
    for (int x = -5; x <= 5; x++) {
      if (abs(x) + abs(y) > 7) continue;
      vec2 off = vec2(float(x), float(y)) * texel * r * 0.4;
      vec3 sCol = IMG_NORM_PIXEL(inputImage, vUv + off).rgb;
      float gate = smoothstep(uThreshold, uThreshold + 0.2, luma(sCol));
      float w = exp(-(float(x*x + y*y)) / (2.0 * r * r));
      bloom += sCol * gate * w;
      wsum += w;
    }
  }
  bloom = (wsum > 0.0) ? bloom / wsum : vec3(0.0);
  bloom *= uHighlightBloom * 1.5;

  // Shadow lift (raises blacks)
  vec3 lifted = src.rgb + (1.0 - src.rgb) * uShadowLift * 0.15;

  // Haze: lower contrast + optional warm tint
  vec3 hazeColor = mix(vec3(0.7, 0.75, 0.8), vec3(0.95, 0.85, 0.7), uHazeWarmth);
  vec3 hazed = mix(lifted, hazeColor, uHaze * 0.3);

  // Combine: hazed base + screen-blended bloom
  vec3 result = 1.0 - (1.0 - hazed) * (1.0 - bloom);
  result = mix(src.rgb, result, uMix * uAmount);

  gl_FragColor = vec4(result, src.a);
}
