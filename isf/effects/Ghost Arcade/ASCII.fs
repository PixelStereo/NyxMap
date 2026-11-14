/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Convert image to ASCII character art. Adapted from Ghost Arcade's ASCII effect.",
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
            "NAME": "cellSizeValue",
            "LABEL": "Cell Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
        },
        {
            "NAME": "contrast",
            "LABEL": "Contrast",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.4,
            "DEFAULT": 1.2
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
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "invert",
            "LABEL": "Invert",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
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
            "MAX": 1.0,
            "DEFAULT": 0.4
        }
    ]
}*/

// Ported from Ghost Arcade (asciiHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uCellSize (cellSizeValue)
#define uContrast (contrast)
#define uColorMix (colorMix)
#define uMode float(floor(modeValue + 0.5))
#define uInvert (invert)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)

      // 4-32 px
      // 0-2
      // 0-1 keep colour
          // 0=density, 1=stipple, 2=block, 3=line
        // 0/1
         // 0-1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

// Approximate ASCII density via geometric primitives
float charShape(vec2 cellUv, float density, int mode) {
  vec2 cu = cellUv - 0.5;
  float r = length(cu);
  if (mode == 0) {
    // Density: ramp through circle/dot/cross/dense
    if (density < 0.1) return 0.0; // .
    if (density < 0.25) return smoothstep(0.45, 0.35, r); // ·
    if (density < 0.45) return smoothstep(0.35, 0.25, r); // o
    if (density < 0.65) return max(smoothstep(0.05, 0.0, abs(cu.x)), smoothstep(0.05, 0.0, abs(cu.y))); // +
    if (density < 0.85) return smoothstep(0.45, 0.0, abs(cu.x) + abs(cu.y) - 0.4); // #
    return 1.0; // @
  } else if (mode == 1) {
    // Stipple: random dots scaled by density
    float h = hash21(floor(cellUv * 8.0));
    return step(1.0 - density, h);
  } else if (mode == 2) {
    // Solid block proportional to density
    return step(1.0 - density, 1.0);
  } else {
    // Line/diagonal hatching
    float ang = density * 3.14;
    float v = abs(sin((cu.x * cos(ang) + cu.y * sin(ang)) * 12.0));
    return step(1.0 - density, v);
  }
}

void main() {
  vec2 cell = floor(vUv * uResolution / uCellSize);
  vec2 cellOrigin = cell * uCellSize / uResolution;
  vec2 cellSize = vec2(uCellSize) / uResolution;
  vec2 cellUv = (vUv - cellOrigin) / cellSize;

  vec3 sampleCol = IMG_NORM_PIXEL(inputImage, cellOrigin + cellSize * 0.5).rgb;
  float l = luma(sampleCol);
  if (uInvert > 0.5) l = 1.0 - l;
  l = clamp((l - 0.5) * uContrast + 0.5, 0.0, 1.0);

  float v = charShape(cellUv, l, int(uMode + 0.5));

  vec3 inkColor = mix(vec3(uTintR, uTintG, uTintB), sampleCol, uColorMix);
  vec3 result = vec3(v) * inkColor;
  gl_FragColor = vec4(result, 1.0);
}
