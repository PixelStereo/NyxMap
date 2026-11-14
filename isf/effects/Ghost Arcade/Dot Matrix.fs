/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Circular dots of varying size based on luminance. Adapted from Ghost Arcade's Dot Matrix effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Text & Pattern",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "dotSize",
            "LABEL": "Dot Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
        },
        {
            "NAME": "dotShape",
            "LABEL": "Dot Shape",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "gapValue",
            "LABEL": "Gap",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "posterize",
            "LABEL": "Posterize",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "glow",
            "LABEL": "Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "bgR",
            "LABEL": "Bg R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "bgG",
            "LABEL": "Bg G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "bgB",
            "LABEL": "Bg B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (dotMatrixHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uDotSize (dotSize)
#define uDotShape float(floor(dotShape + 0.5))
#define uGap (gapValue)
#define uPosterize (posterize)
#define uGlow (glow)
#define uBgR (bgR)
#define uBgG (bgG)
#define uBgB (bgB)

       // 4-32
      // 0=circle, 1=square, 2=hex
           // 0-1 gap between dots
     // 1-8 quantize per channel
          // 0-1

void main() {
  vec2 cell = floor(vUv * uResolution / uDotSize);
  vec2 cellOrigin = cell * uDotSize / uResolution;
  vec2 cellSize = vec2(uDotSize) / uResolution;
  vec2 cellUv = (vUv - cellOrigin) / cellSize - 0.5;

  int shape = int(uDotShape + 0.5);
  float dotR = mix(0.45, 0.5 - uGap * 0.5, 0.5);
  float mask;
  if (shape == 0) {
    mask = smoothstep(dotR + 0.05, dotR - 0.05, length(cellUv));
  } else if (shape == 1) {
    vec2 ad = abs(cellUv);
    mask = smoothstep(dotR + 0.02, dotR - 0.02, max(ad.x, ad.y));
  } else {
    // Hex
    vec2 ad = abs(cellUv);
    float hex = max(ad.x * 0.866 + ad.y * 0.5, ad.y);
    mask = smoothstep(dotR + 0.02, dotR - 0.02, hex);
  }

  vec3 sampleCol = IMG_NORM_PIXEL(inputImage, cellOrigin + cellSize * 0.5).rgb;
  if (uPosterize > 1.001) {
    float steps = max(1.0, uPosterize);
    sampleCol = floor(sampleCol * steps + 0.5) / steps;
  }

  vec3 bg = vec3(uBgR, uBgG, uBgB);
  vec3 result = mix(bg, sampleCol, mask);

  // Glow halo
  if (uGlow > 0.001) {
    float halo = smoothstep(0.7, 0.45, length(cellUv));
    result += sampleCol * halo * uGlow * 0.4;
  }
  gl_FragColor = vec4(result, 1.0);
}
