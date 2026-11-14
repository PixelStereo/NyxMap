/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Reduce color levels for a poster look. Adapted from Ghost Arcade's Posterize effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Color",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "posterizeLevels",
            "LABEL": "Levels",
            "TYPE": "float",
            "MIN": 2.0,
            "MAX": 16.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "ditherAmount",
            "LABEL": "Dither Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "animSpeed",
            "LABEL": "Anim Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "paletteLock",
            "LABEL": "Palette Lock",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (posterizeShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uLevels (posterizeLevels)
#define uDitherAmount (ditherAmount)
#define uAnimSpeed (animSpeed)
#define uPaletteLock (paletteLock)

        // 2-32 color levels per channel
  // 0-1 — Bayer 4×4 ordered dither strength
     // 0-2 — animated level stepping speed (0 = static)
   // 0=free RGB, 1=comic, 2=thermal, 3=retro 4-color

// Bayer 4×4 dither matrix in [0..1] range (after /16).
float bayer4(vec2 pos) {
  int x = int(mod(pos.x, 4.0));
  int y = int(mod(pos.y, 4.0));
  int idx = x + y * 4;
  // Unrolled Bayer 4x4 (standard ordering scaled by 1/16).
  float m =
    idx == 0  ?  0.0 :
    idx == 1  ?  8.0 :
    idx == 2  ?  2.0 :
    idx == 3  ? 10.0 :
    idx == 4  ? 12.0 :
    idx == 5  ?  4.0 :
    idx == 6  ? 14.0 :
    idx == 7  ?  6.0 :
    idx == 8  ?  3.0 :
    idx == 9  ? 11.0 :
    idx == 10 ?  1.0 :
    idx == 11 ?  9.0 :
    idx == 12 ? 15.0 :
    idx == 13 ?  7.0 :
    idx == 14 ? 13.0 : 5.0;
  return (m + 0.5) / 16.0 - 0.5;
}

// Snap an RGB color to the nearest entry in a fixed palette.
vec3 snapToPalette(vec3 c, int palette) {
  if (palette == 1) {
    // Comic: 6 bold flat colors — black, red, yellow, green, blue, white.
    vec3 palC[6];
    palC[0] = vec3(0.05, 0.05, 0.05);
    palC[1] = vec3(0.85, 0.10, 0.15);
    palC[2] = vec3(0.95, 0.85, 0.10);
    palC[3] = vec3(0.20, 0.65, 0.30);
    palC[4] = vec3(0.10, 0.30, 0.85);
    palC[5] = vec3(0.96, 0.96, 0.96);
    vec3 best = palC[0];
    float bestD = 1e9;
    for (int i = 0; i < 6; i++) {
      float d = dot(c - palC[i], c - palC[i]);
      if (d < bestD) { bestD = d; best = palC[i]; }
    }
    return best;
  } else if (palette == 2) {
    // Thermal: 5-stop heatmap — black, blue, magenta, orange, white.
    vec3 palT[5];
    palT[0] = vec3(0.0, 0.0, 0.05);
    palT[1] = vec3(0.05, 0.10, 0.55);
    palT[2] = vec3(0.65, 0.15, 0.55);
    palT[3] = vec3(0.95, 0.45, 0.10);
    palT[4] = vec3(0.98, 0.96, 0.85);
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    float idx = clamp(lum * 4.0, 0.0, 4.0);
    int i0 = int(floor(idx));
    int i1 = min(i0 + 1, 4);
    return mix(palT[i0], palT[i1], fract(idx));
  } else {
    // Retro 4-color (Game Boy / classic LCD): dark green, mid green, light green, cream.
    vec3 palR[4];
    palR[0] = vec3(0.10, 0.20, 0.10);
    palR[1] = vec3(0.30, 0.45, 0.25);
    palR[2] = vec3(0.55, 0.70, 0.40);
    palR[3] = vec3(0.85, 0.92, 0.70);
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    float idx = clamp(lum * 3.0, 0.0, 3.0);
    int i0 = int(floor(idx));
    int i1 = min(i0 + 1, 3);
    return mix(palR[i0], palR[i1], fract(idx));
  }
}

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);

  // Animated stepping — levels oscillates between half and full
  // when uAnimSpeed > 0. Creates a pulsing posterize.
  float animLevels = uLevels;
  if (uAnimSpeed > 0.001) {
    float t = sin(uTime * uAnimSpeed * 3.14159) * 0.5 + 0.5;
    animLevels = mix(2.0, uLevels, t);
  }
  float levels = max(2.0, floor(animLevels));

  // Apply Bayer dither BEFORE quantization to break up banding.
  vec3 src = texColor.rgb;
  if (uDitherAmount > 0.001) {
    vec2 pxPos = vUv * uResolution;
    float d = bayer4(pxPos);
    src = clamp(src + d * uDitherAmount / levels, 0.0, 1.0);
  }

  vec3 posterized;
  int paletteMode = int(uPaletteLock + 0.5);
  if (paletteMode == 0) {
    // Free RGB quantization (legacy behavior).
    posterized = floor(src * levels) / (levels - 1.0);
  } else {
    // Snap to a fixed palette — uLevels controls smoothness of the
    // luma→palette interpolation indirectly through the dither.
    posterized = snapToPalette(src, paletteMode);
  }

  gl_FragColor = vec4(posterized, texColor.a);
}
