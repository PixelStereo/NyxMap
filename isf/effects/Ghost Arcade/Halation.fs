/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Warm film halation — red glow bleeding from highlights. Adapted from Ghost Arcade's Halation effect.",
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
            "MAX": 1.2,
            "DEFAULT": 0.6
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
            "MAX": 1.3,
            "DEFAULT": 0.65
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.8,
            "DEFAULT": 0.9
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.45
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
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
            "NAME": "mixValue",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (halationHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uRadius (radiusValue)
#define uThreshold (thresholdValue)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uMode float(floor(modeValue + 0.5))
#define uMix (mixValue)

        // 0-2 bleed strength
        // 0-30 bleed radius
     // 0-1 highlight threshold
         // 0-1 bleed colour
         // 0-1
         // 0-1
          // 0=screen, 1=add, 2=soft light
           // 0-1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uAmount < 0.001) { gl_FragColor = src; return; }

  vec2 texel = 1.0 / uResolution;
  vec3 bleed = vec3(0.0);
  float wsum = 0.0;
  float r = max(1.0, uRadius);

  for (int y = -5; y <= 5; y++) {
    for (int x = -5; x <= 5; x++) {
      if (abs(x) + abs(y) > 7) continue;
      vec2 off = vec2(float(x), float(y)) * texel * r * 0.5;
      vec3 sampleCol = IMG_NORM_PIXEL(inputImage, vUv + off).rgb;
      float gate = smoothstep(uThreshold, uThreshold + 0.2, luma(sampleCol));
      float w = exp(-(float(x*x + y*y)) / (2.0 * r * r));
      bleed += sampleCol * gate * w;
      wsum += w;
    }
  }
  bleed = (wsum > 0.0) ? bleed / wsum : vec3(0.0);
  bleed *= vec3(uTintR, uTintG, uTintB) * uAmount;

  int mode = int(uMode + 0.5);
  vec3 result;
  if (mode == 0) {
    // Screen
    result = 1.0 - (1.0 - src.rgb) * (1.0 - bleed);
  } else if (mode == 1) {
    // Add
    result = src.rgb + bleed;
  } else {
    // Soft light
    vec3 a = 2.0 * src.rgb * bleed + src.rgb * src.rgb * (1.0 - 2.0 * bleed);
    vec3 b = sqrt(src.rgb) * (2.0 * bleed - 1.0) + 2.0 * src.rgb * (1.0 - bleed);
    result = mix(a, b, step(0.5, bleed));
  }

  gl_FragColor = vec4(mix(src.rgb, result, uMix), src.a);
}
