/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Map luminance to cycling color palettes — named palettes, audio cycling, posterized bands. Adapted from Ghost Arcade's Colorama effect.",
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
            "NAME": "paletteValue",
            "LABEL": "Palette",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "coloramaOffset",
            "LABEL": "Offset",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "coloramaSpeed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "coloramaContrast",
            "LABEL": "Contrast",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "coloramaMix",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "bandsValue",
            "LABEL": "Bands",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "hueShift",
            "LABEL": "Hue Shift",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (coloramaShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uPalette float(floor(paletteValue + 0.5))
#define uOffset (coloramaOffset)
#define uSpeed (coloramaSpeed)
#define uContrast (coloramaContrast)
#define uMix (coloramaMix)
#define uBands (bandsValue)
#define uAudioReact 0.0
#define uHueShift (hueShift)
#define uAudio 0.0

       // 0-11 named palettes (Rainbow, Sunset, Ocean, Neon, Fire,
                              //   Forest, Ice, Psychedelic, Vaporwave, Club, Pastel, Mono)
        // 0-1 manual offset through palette
         // 0-2 auto-cycle speed (0 = off)
      // 0.5-2 luminance contrast
           // 0-1 blend with original
         // 0 = smooth gradient, 1-32 = posterized into N bands
    // 0-1 how much audio modulates the cycling (0 = ignore audio)
      // 0-1 fixed rotation through palette (separate from auto-cycle)
         // 0-1 live audio rms (set by renderer per frame)

#define PI 3.14159265359
#define TAU 6.28318530718

// Cosine palette function: a + b * cos(2π * (c * t + d))
vec3 cosinePalette(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
  return a + b * cos(TAU * (c * t + d));
}

// 12 named palettes — coefficients for the cosine-palette formula.
// Indices here MUST match the dropdown labels in effectUX.ts.
vec3 getPaletteColor(float t, int palette) {
  vec3 a, b, c, d;

  if (palette == 0) {
    // 0 — Rainbow (classic full-spectrum cycle)
    a = vec3(0.5, 0.5, 0.5);
    b = vec3(0.5, 0.5, 0.5);
    c = vec3(1.0, 1.0, 1.0);
    d = vec3(0.0, 0.33, 0.67);
  }
  else if (palette == 1) {
    // 1 — Sunset (warm oranges, magentas, deep purples)
    a = vec3(0.5, 0.5, 0.5);
    b = vec3(0.5, 0.5, 0.5);
    c = vec3(1.0, 1.0, 1.0);
    d = vec3(0.0, 0.1, 0.2);
  }
  else if (palette == 2) {
    // 2 — Ocean (teals and deep blues)
    a = vec3(0.5, 0.5, 0.5);
    b = vec3(0.5, 0.5, 0.5);
    c = vec3(1.0, 1.0, 1.0);
    d = vec3(0.3, 0.2, 0.2);
  }
  else if (palette == 3) {
    // 3 — Neon (vibrant pinks, cyans)
    a = vec3(0.5, 0.5, 0.5);
    b = vec3(0.5, 0.5, 0.5);
    c = vec3(1.0, 1.0, 0.5);
    d = vec3(0.8, 0.9, 0.3);
  }
  else if (palette == 4) {
    // 4 — Fire (reds → oranges → yellows)
    a = vec3(0.5, 0.5, 0.5);
    b = vec3(0.5, 0.5, 0.5);
    c = vec3(1.0, 0.7, 0.4);
    d = vec3(0.0, 0.15, 0.2);
  }
  else if (palette == 5) {
    // 5 — Forest (greens with earthy browns)
    a = vec3(0.5, 0.5, 0.5);
    b = vec3(0.5, 0.5, 0.5);
    c = vec3(1.0, 1.0, 1.0);
    d = vec3(0.0, 0.1, 0.0);
  }
  else if (palette == 6) {
    // 6 — Ice (whites, blues, cyan highlights)
    a = vec3(0.8, 0.8, 0.9);
    b = vec3(0.2, 0.4, 0.2);
    c = vec3(1.0, 1.0, 1.0);
    d = vec3(0.0, 0.25, 0.25);
  }
  else if (palette == 7) {
    // 7 — Psychedelic (rapid hue swings)
    a = vec3(0.5, 0.5, 0.5);
    b = vec3(0.5, 0.5, 0.5);
    c = vec3(2.0, 1.0, 0.0);
    d = vec3(0.5, 0.2, 0.25);
  }
  else if (palette == 8) {
    // 8 — Vaporwave (hot pink → purple → teal — 80s synth aesthetic)
    a = vec3(0.6, 0.4, 0.7);
    b = vec3(0.4, 0.4, 0.4);
    c = vec3(1.0, 1.0, 0.5);
    d = vec3(0.0, 0.15, 0.50);
  }
  else if (palette == 9) {
    // 9 — Club (saturated cyan/magenta/yellow stage-light cycle)
    a = vec3(0.55, 0.45, 0.55);
    b = vec3(0.55, 0.5, 0.5);
    c = vec3(1.5, 1.5, 1.0);
    d = vec3(0.0, 0.5, 0.85);
  }
  else if (palette == 10) {
    // 10 — Pastel (soft pinks, mint, baby blue)
    a = vec3(0.85, 0.8, 0.85);
    b = vec3(0.15, 0.18, 0.15);
    c = vec3(1.0, 1.0, 1.0);
    d = vec3(0.0, 0.33, 0.67);
  }
  else {
    // 11 — Mono Glow (single-hue luminance ramp, hue picked by uHueShift)
    // Use uHueShift as a hue offset on a simple HSV-style cycle. We
    // approximate this with a cosine palette whose d-vector is shifted.
    float h = mod(uHueShift, 1.0);
    a = vec3(0.5);
    b = vec3(0.5);
    c = vec3(1.0);
    d = vec3(h, h + 0.33, h + 0.67);
  }

  return cosinePalette(t, a, b, c, d);
}

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 color = texColor.rgb;

  // Calculate luminance
  float lum = dot(color, vec3(0.299, 0.587, 0.114));

  // Apply contrast to luminance
  lum = (lum - 0.5) * uContrast + 0.5;
  lum = clamp(lum, 0.0, 1.0);

  // Posterize: snap luminance to discrete bands BEFORE palette lookup.
  if (uBands >= 0.5) {
    float steps = floor(uBands + 0.5);
    lum = floor(lum * steps) / max(steps - 1.0, 1.0);
    lum = clamp(lum, 0.0, 1.0);
  }

  // Audio reactivity: live audio RMS modulates the cycling offset.
  float audioPunch = clamp(uAudio, 0.0, 1.5) * uAudioReact;

  // Total palette parameter: luminance + manual offset + auto-cycle
  // + hue shift + audio punch.
  float t = lum + uOffset + uTime * uSpeed + uHueShift + audioPunch;

  // Get palette color
  int paletteIndex = int(uPalette);
  vec3 paletteColor = getPaletteColor(t, paletteIndex);

  // Mix with original based on mix parameter
  vec3 finalColor = mix(color, paletteColor, uMix);

  gl_FragColor = vec4(finalColor, texColor.a);
}
