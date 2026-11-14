/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Sci-fi hologram look: projector grid + descending scan beam + RGB flicker + broken bands + edge glow. Adapted from Ghost Arcade's Hologram Scan effect.",
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
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "scanFreq",
            "LABEL": "Scan Freq",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 400.0,
            "DEFAULT": 200.0
        },
        {
            "NAME": "scanSpeed",
            "LABEL": "Scan Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "gridSpacing",
            "LABEL": "Grid Spacing",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
        },
        {
            "NAME": "rGBFlicker",
            "LABEL": "RGBFlicker",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "brokenBands",
            "LABEL": "Broken Bands",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.9,
            "DEFAULT": 0.95
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
            "NAME": "opacityFlicker",
            "LABEL": "Opacity Flicker",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "edgeGlow",
            "LABEL": "Edge Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        }
    ]
}*/

// Ported from Ghost Arcade (hologramScanHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (intensity)
#define uScanFreq (scanFreq)
#define uScanSpeed (scanSpeed)
#define uGridSpacing (gridSpacing)
#define uRGBFlicker (rGBFlicker)
#define uBrokenBands (brokenBands)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uOpacityFlicker (opacityFlicker)
#define uEdgeGlow (edgeGlow)

     // 0-1
      // 50-500
     // 0-3
   // 4-32
    // 0-1
   // 0-1 random horizontal dropouts



// 0-1
      // 0-1

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec3 tint = vec3(uTintR, uTintG, uTintB);
  vec3 col = src;

  // RGB channel flicker (each channel jitters independently)
  if (uRGBFlicker > 0.001) {
    float t = floor(uTime * 30.0);
    float r = (hash21(vec2(t, 0)) - 0.5) * uRGBFlicker * 0.04;
    float g = (hash21(vec2(t, 1)) - 0.5) * uRGBFlicker * 0.04;
    float b = (hash21(vec2(t, 2)) - 0.5) * uRGBFlicker * 0.04;
    col.r = IMG_NORM_PIXEL(inputImage, vUv + vec2(r, 0)).r;
    col.g = IMG_NORM_PIXEL(inputImage, vUv + vec2(g, 0)).g;
    col.b = IMG_NORM_PIXEL(inputImage, vUv + vec2(b, 0)).b;
  }

  // Scanline + descending scan beam
  float scan = sin(vUv.y * uScanFreq * 3.14159 - uTime * uScanSpeed * 4.0);
  scan = mix(1.0, scan * 0.4 + 0.6, uIntensity);
  col *= scan;

  // Bright moving scan beam
  float beam = smoothstep(0.04, 0.0, abs(vUv.y - mod(uTime * uScanSpeed * 0.3, 1.0)));
  col += beam * tint * uIntensity * 1.5;

  // Grid overlay
  if (uGridSpacing > 0.5) {
    vec2 g = mod(vUv * uResolution, uGridSpacing);
    float gridLine = step(uGridSpacing - 1.0, max(g.x, g.y));
    col += gridLine * tint * 0.2 * uIntensity;
  }

  // Broken bands (horizontal dropouts)
  if (uBrokenBands > 0.001) {
    float bandY = floor(vUv.y * 60.0 + uTime * 2.0);
    float dropout = step(0.94, hash21(vec2(bandY, floor(uTime * 4.0))));
    col *= 1.0 - dropout * uBrokenBands * 0.6;
  }

  // Holographic tint
  col = mix(col, col * tint + tint * 0.15, uIntensity * 0.5);

  // Edge glow
  if (uEdgeGlow > 0.001) {
    vec2 texel = 1.0 / uResolution;
    float l = luma(src);
    float lN = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(0.0, 1.0)).rgb);
    float lE = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(1.0, 0.0)).rgb);
    float edge = abs(l - lN) + abs(l - lE);
    col += tint * edge * uEdgeGlow * 2.0;
  }

  // Overall opacity flicker
  if (uOpacityFlicker > 0.001) {
    float opf = 1.0 - (sin(uTime * 8.0) * 0.5 + 0.5) * uOpacityFlicker * 0.3;
    col *= opf;
  }

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
