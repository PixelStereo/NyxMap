/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Luma-derived contour lines become physical displacement ridges with shadow + chromatic edge. Adapted from Ghost Arcade's Topographic Depth Warp effect.",
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
            "NAME": "contourCount",
            "LABEL": "Contour Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
        },
        {
            "NAME": "contourWidth",
            "LABEL": "Contour Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.008
        },
        {
            "NAME": "displacement",
            "LABEL": "Displacement",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "chromaticEdge",
            "LABEL": "Chromatic Edge",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "colorR",
            "LABEL": "Color R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "colorG",
            "LABEL": "Color G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
        },
        {
            "NAME": "colorB",
            "LABEL": "Color B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "shadowRidges",
            "LABEL": "Shadow Ridges",
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
            "MAX": 1.7,
            "DEFAULT": 0.85
        }
    ]
}*/

// Ported from Ghost Arcade (topoWarpHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uContourCount (contourCount)
#define uContourWidth (contourWidth)
#define uDisplacement (displacement)
#define uChromaticEdge (chromaticEdge)
#define uColorR (colorR)
#define uColorG (colorG)
#define uColorB (colorB)
#define uShadowRidges (shadowRidges)
#define uMix (mixValue)

  // 4-32
  // 0.001-0.05
  // 0-1
 // 0-1
        // contour line color


  // 0-1
           // 0-1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec2 texel = 1.0 / uResolution;

  // Estimate gradient of luma
  float l = luma(src);
  float lE = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(1, 0)).rgb);
  float lN = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(0, 1)).rgb);
  vec2 grad = vec2(lE - l, lN - l);

  // Contour bands
  float bands = max(1.0, uContourCount);
  float bandPos = fract(l * bands);
  float ridge = smoothstep(uContourWidth * 5.0, 0.0, abs(bandPos - 0.5));

  // Displacement: shift sample_ along gradient by ridge
  vec2 disp = grad * ridge * uDisplacement * 0.3;
  vec3 col;
  if (uChromaticEdge > 0.001) {
    col.r = IMG_NORM_PIXEL(inputImage, vUv + disp * (1.0 + uChromaticEdge * 0.5)).r;
    col.g = IMG_NORM_PIXEL(inputImage, vUv + disp).g;
    col.b = IMG_NORM_PIXEL(inputImage, vUv + disp * (1.0 - uChromaticEdge * 0.5)).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, vUv + disp).rgb;
  }

  // Contour line overlay
  vec3 contour = vec3(uColorR, uColorG, uColorB) * ridge;

  // Shadow ridges (darker on one side of contour)
  if (uShadowRidges > 0.001) {
    float side = step(0.5, bandPos);
    col *= 1.0 - side * ridge * uShadowRidges * 0.5;
  }

  vec3 result = mix(src, col + contour, uMix);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
