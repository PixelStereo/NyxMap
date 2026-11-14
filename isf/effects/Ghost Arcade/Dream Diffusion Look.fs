/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Soft bloom + halation + chromatic blur + pastel highlight rolloff + shadow lift. Hazy dream-like grade. Adapted from Ghost Arcade's Dream Diffusion Look effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "New Hero",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "bloomAmount",
            "LABEL": "Bloom Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.4,
            "DEFAULT": 1.2
        },
        {
            "NAME": "bloomRadius",
            "LABEL": "Bloom Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 28.0,
            "DEFAULT": 14.0
        },
        {
            "NAME": "halation",
            "LABEL": "Halation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "chromaticBlur",
            "LABEL": "Chromatic Blur",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "pastelRolloff",
            "LABEL": "Pastel Rolloff",
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
            "DEFAULT": 0.15
        },
        {
            "NAME": "softnessValue",
            "LABEL": "Softness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.1,
            "DEFAULT": 1.05
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.9,
            "DEFAULT": 0.95
        }
    ]
}*/

// Ported from Ghost Arcade (dreamDiffusionHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uBloomAmount (bloomAmount)
#define uBloomRadius (bloomRadius)
#define uHalation (halation)
#define uChromaticBlur (chromaticBlur)
#define uPastelRolloff (pastelRolloff)
#define uShadowLift (shadowLift)
#define uSoftness (softnessValue)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)

   // 0-2
   // 1-30
      // 0-1
 // 0-1
 // 0-1 desaturate highlights toward pastel
    // 0-0.5
      // 0-1 overall softness

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec2 texel = 1.0 / uResolution;

  // Chromatic blur per channel
  vec3 cbCol = src;
  if (uChromaticBlur > 0.001) {
    float cb = uChromaticBlur * 6.0;
    vec3 acc = vec3(0.0);
    float wsum = 0.0;
    for (int y = -2; y <= 2; y++) {
      for (int x = -2; x <= 2; x++) {
        vec2 off = vec2(float(x), float(y)) * texel * cb;
        float w = exp(-(float(x*x + y*y)) / 4.0);
        acc.r += IMG_NORM_PIXEL(inputImage, vUv + off * 1.05).r * w;
        acc.g += IMG_NORM_PIXEL(inputImage, vUv + off).g * w;
        acc.b += IMG_NORM_PIXEL(inputImage, vUv + off * 0.95).b * w;
        wsum += w;
      }
    }
    cbCol = acc / wsum;
  }

  // Bloom on highlights
  vec3 bloom = vec3(0.0);
  float wsum2 = 0.0;
  for (int y = -3; y <= 3; y++) {
    for (int x = -3; x <= 3; x++) {
      if (abs(x) + abs(y) > 4) continue;
      vec2 off = vec2(float(x), float(y)) * texel * uBloomRadius * 0.4;
      vec3 s = IMG_NORM_PIXEL(inputImage, vUv + off).rgb;
      float gate = smoothstep(0.55, 0.85, luma(s));
      float w = exp(-(float(x*x + y*y)) / 8.0);
      bloom += s * gate * w;
      wsum2 += w;
    }
  }
  bloom = (wsum2 > 0.001) ? bloom / wsum2 : vec3(0.0);
  bloom *= uBloomAmount;

  // Halation (warm bleed around highlights)
  vec3 halo = bloom * vec3(1.0, 0.6, 0.4) * uHalation;

  // Pastel rolloff: desaturate highlights toward white
  float l = luma(cbCol);
  if (uPastelRolloff > 0.001) {
    vec3 pastel = mix(cbCol, vec3(1.0), smoothstep(0.7, 1.0, l) * uPastelRolloff);
    cbCol = pastel;
  }

  // Shadow lift
  cbCol += vec3(uShadowLift) * (1.0 - smoothstep(0.0, 0.4, l));

  // Combine
  vec3 result = cbCol + bloom + halo;
  result *= vec3(uTintR, uTintG, uTintB);
  // Overall softness via slight blur mix
  result = mix(result, mix(result, src, 0.5), uSoftness * 0.3);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
