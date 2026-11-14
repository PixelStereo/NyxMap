/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Comic book ink lines with halftone shading. Adapted from Ghost Arcade's Comic Ink effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Stylize",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "inkStrength",
            "LABEL": "Ink Strength",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.4,
            "DEFAULT": 1.2
        },
        {
            "NAME": "inkThreshold",
            "LABEL": "Ink Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "posterize",
            "LABEL": "Posterize",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 10.0,
            "DEFAULT": 5.0
        },
        {
            "NAME": "halftoneShadow",
            "LABEL": "Halftone Shadow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "halftoneSize",
            "LABEL": "Halftone Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 12.0,
            "DEFAULT": 6.0
        },
        {
            "NAME": "colorMix",
            "LABEL": "Color Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "inkR",
            "LABEL": "Ink R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "inkG",
            "LABEL": "Ink G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "inkB",
            "LABEL": "Ink B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (comicInkHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uInkStrength (inkStrength)
#define uInkThreshold (inkThreshold)
#define uPosterize (posterize)
#define uHalftoneShadow (halftoneShadow)
#define uHalftoneSize (halftoneSize)
#define uColorMix (colorMix)
#define uInkR (inkR)
#define uInkG (inkG)
#define uInkB (inkB)

   // 0-2
  // 0-1
     // 2-12 levels
// 0-1
  // 2-16
      // 0-1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec2 texel = 1.0 / uResolution;
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;

  // Sobel edges on luma
  float l00 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(-1, -1)).rgb);
  float l10 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 0, -1)).rgb);
  float l20 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 1, -1)).rgb);
  float l01 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(-1,  0)).rgb);
  float l21 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 1,  0)).rgb);
  float l02 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(-1,  1)).rgb);
  float l12 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 0,  1)).rgb);
  float l22 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 1,  1)).rgb);
  float gx = (l20 + 2.0 * l21 + l22) - (l00 + 2.0 * l01 + l02);
  float gy = (l02 + 2.0 * l12 + l22) - (l00 + 2.0 * l10 + l20);
  float edge = clamp(length(vec2(gx, gy)) * uInkStrength, 0.0, 1.0);
  float ink = step(uInkThreshold, edge);

  // Posterize
  float steps = max(2.0, uPosterize);
  vec3 quant = floor(src * steps + 0.5) / steps;
  vec3 colored = mix(quant, src, uColorMix);

  // Halftone shadow overlay
  if (uHalftoneShadow > 0.001) {
    float l = luma(quant);
    vec2 px = vUv * uResolution / uHalftoneSize;
    vec2 cell = fract(px) - 0.5;
    float dot = smoothstep(0.45, 0.4, length(cell)) * (1.0 - l);
    colored *= 1.0 - dot * uHalftoneShadow;
  }

  vec3 result = mix(colored, vec3(uInkR, uInkG, uInkB), ink);
  gl_FragColor = vec4(result, 1.0);
}
