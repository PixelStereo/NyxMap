/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Thermal palette (Ironbow / Jet / Viridis / Inferno) plus contour isolines and bright-blob tracking. Adapted from Ghost Arcade's Thermal Contour effect.",
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
            "NAME": "palette",
            "LABEL": "Palette",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "contourCount",
            "LABEL": "Contour Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 16.0,
            "DEFAULT": 8.0
        },
        {
            "NAME": "contourWidth",
            "LABEL": "Contour Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.005
        },
        {
            "NAME": "contourGlow",
            "LABEL": "Contour Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "trackBlobs",
            "LABEL": "Track Blobs",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
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

// Ported from Ghost Arcade (thermalContourHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uPalette float(floor(palette + 0.5))
#define uContourCount (contourCount)
#define uContourWidth (contourWidth)
#define uContourGlow (contourGlow)
#define uIntensity (intensity)
#define uTrackBlobs (trackBlobs)
#define uMix (mixValue)

       // 0=ironbow, 1=jet, 2=viridis, 3=inferno
  // 1-12 isolines
  // 0.001-0.02
   // 0-1
     // 0-2
    // 0-1 highlight bright clusters
           // 0-1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 paletteCol(float t) {
  int p = int(uPalette + 0.5);
  if (p == 0) {
    // Ironbow
    vec3 c = mix(vec3(0, 0, 0.3), vec3(0.5, 0, 0.5), smoothstep(0.0, 0.25, t));
    c = mix(c, vec3(1, 0.3, 0), smoothstep(0.25, 0.55, t));
    c = mix(c, vec3(1, 1, 0.2), smoothstep(0.55, 0.85, t));
    c = mix(c, vec3(1, 1, 1), smoothstep(0.85, 1.0, t));
    return c;
  } else if (p == 1) {
    // Jet
    return vec3(
      smoothstep(0.35, 0.65, t) - smoothstep(0.85, 1.0, t),
      smoothstep(0.0, 0.35, t) - smoothstep(0.65, 1.0, t),
      smoothstep(0.0, 0.15, t) - smoothstep(0.5, 0.7, t)
    );
  } else if (p == 2) {
    // Viridis
    return vec3(0.27 + 0.5 * t, 0.005 + 0.9 * t, 0.33 + 0.5 * (1.0 - t));
  } else {
    // Inferno
    vec3 c = mix(vec3(0, 0, 0), vec3(0.4, 0, 0.4), smoothstep(0.0, 0.3, t));
    c = mix(c, vec3(0.95, 0.4, 0.1), smoothstep(0.3, 0.65, t));
    c = mix(c, vec3(1, 1, 0.6), smoothstep(0.65, 1.0, t));
    return c;
  }
}

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  float l = luma(src);
  vec3 thermal = paletteCol(l) * uIntensity;

  // Contour isolines
  float cw = max(0.001, uContourWidth);
  float bands = max(1.0, uContourCount);
  float bandPos = fract(l * bands);
  float contour = smoothstep(cw, 0.0, abs(bandPos - 0.5)) * uContourGlow;
  thermal += vec3(1.0) * contour;

  // Track blobs: highlight luma clusters
  if (uTrackBlobs > 0.001) {
    vec2 texel = 1.0 / uResolution;
    float lN = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(0, 4)).rgb);
    float lS = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(0, -4)).rgb);
    float lE = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(4, 0)).rgb);
    float lW = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(-4, 0)).rgb);
    float gradMag = abs(l - lN) + abs(l - lS) + abs(l - lE) + abs(l - lW);
    thermal += vec3(0.2, 1.0, 0.8) * smoothstep(0.6, 1.0, l) * uTrackBlobs * (1.0 - gradMag);
  }

  vec3 result = mix(src, thermal, uMix);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
