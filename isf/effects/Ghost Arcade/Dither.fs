/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Retro dithering patterns — Bayer, noise, halftone. Adapted from Ghost Arcade's Dither effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Stylize",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "typeValue",
            "LABEL": "Type",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "ditherIntensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "ditherScale",
            "LABEL": "Scale",
            "TYPE": "float",
            "MIN": 1.0,
            "MAX": 8.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "colorDepth",
            "LABEL": "Color Depth",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 4.0,
            "DEFAULT": 2.0
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
            "NAME": "pixelLock",
            "LABEL": "Pixel Lock",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (ditherShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uType float(floor(typeValue + 0.5))
#define uIntensity (ditherIntensity)
#define uScale (ditherScale)
#define uColorDepth (colorDepth)
#define uPalette float(floor(paletteValue + 0.5))
#define uPixelLock (pixelLock)

        // 0=bayer, 1=blueNoise, 2=halftone, 3=atkinson, 4=floydSteinberg
   // 0-1
       // 1-16
  // 1-8 bits
     // 0=free, 1=mono, 2=CGA-4, 3=EGA-8, 4=GameBoy, 5=Amber-CRT
   // 0=continuous, 1=snap to integer pixel grid (no aliasing on resize)

// Snap to nearest fixed palette color. uses simple Euclidean distance.
vec3 snapPalette(vec3 c, int pal) {
  if (pal == 1) {
    // 1-bit mono — snap to black or white by luma.
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    return lum > 0.5 ? vec3(1.0) : vec3(0.0);
  }
  if (pal == 2) {
    // CGA mode 4 palette 1 — black, cyan, magenta, white.
    vec3 P[4]; P[0]=vec3(0.0); P[1]=vec3(0.0,1.0,1.0); P[2]=vec3(1.0,0.0,1.0); P[3]=vec3(1.0);
    vec3 best = P[0]; float bd = 1e9;
    for (int i = 0; i < 4; i++) { float d = dot(c-P[i], c-P[i]); if (d < bd) { bd = d; best = P[i]; } }
    return best;
  }
  if (pal == 3) {
    // EGA 8-color (high-intensity).
    vec3 P[8];
    P[0]=vec3(0.0); P[1]=vec3(0.0,0.0,0.67); P[2]=vec3(0.0,0.67,0.0); P[3]=vec3(0.0,0.67,0.67);
    P[4]=vec3(0.67,0.0,0.0); P[5]=vec3(0.67,0.0,0.67); P[6]=vec3(0.67,0.33,0.0); P[7]=vec3(0.67);
    vec3 best = P[0]; float bd = 1e9;
    for (int i = 0; i < 8; i++) { float d = dot(c-P[i], c-P[i]); if (d < bd) { bd = d; best = P[i]; } }
    return best;
  }
  if (pal == 4) {
    // Game Boy 4-shade green.
    vec3 P[4];
    P[0]=vec3(0.06,0.22,0.06); P[1]=vec3(0.19,0.38,0.19); P[2]=vec3(0.55,0.67,0.06); P[3]=vec3(0.61,0.74,0.06);
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    int idx = int(clamp(floor(lum * 4.0), 0.0, 3.0));
    return P[idx];
  }
  if (pal == 5) {
    // Amber CRT — black, dim amber, mid amber, bright amber.
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    return mix(vec3(0.0), vec3(1.0, 0.65, 0.0), smoothstep(0.0, 1.0, lum));
  }
  return c; // pal == 0 free
}

// High-quality Bayer 8x8 matrix with proper thresholds
float bayer8(vec2 pos) {
  vec2 p = mod(pos, 8.0);
  float x = p.x;
  float y = p.y;

  // Recursive Bayer matrix calculation (much cleaner than lookup)
  float threshold = 0.0;
  float divisor = 64.0;

  // 8x8 Bayer using bit manipulation logic
  for (int i = 0; i < 3; i++) {
    float mx = mod(x, 2.0);
    float my = mod(y, 2.0);
    threshold += (mx + my * 2.0) * divisor / 4.0;
    divisor /= 4.0;
    x = floor(x / 2.0);
    y = floor(y / 2.0);
  }

  return threshold / 64.0;
}

// Blue noise approximation using layered randomness
float blueNoise(vec2 pos) {
  float n = 0.0;
  float scale = 1.0;

  for (int i = 0; i < 4; i++) {
    vec2 p = pos * scale;
    float r = fract(sin(dot(floor(p), vec2(12.9898, 78.233) + float(i) * 100.0)) * 43758.5453);
    n += r / scale;
    scale *= 2.0;
  }

  return fract(n * 0.25 + uTime * 0.01);
}

// Premium halftone with angle and smooth dots
float halftone(vec2 pos, float angle) {
  float c = cos(angle);
  float s = sin(angle);
  mat2 rot = mat2(c, -s, s, c);
  vec2 p = rot * pos;

  vec2 nearest = floor(p) + 0.5;
  float dist = length(p - nearest);

  // Smooth dot with antialiasing
  return smoothstep(0.5, 0.3, dist);
}

// Atkinson-style dithering pattern (used by old Mac)
float atkinsonPattern(vec2 pos) {
  vec2 p = mod(pos, 4.0);
  float pattern = 0.0;

  // Classic Atkinson-style sparse pattern
  if ((p.x < 2.0 && p.y < 2.0) || (p.x >= 2.0 && p.y >= 2.0)) {
    pattern = mod(p.x + p.y, 2.0);
  } else {
    pattern = 1.0 - mod(p.x + p.y, 2.0);
  }

  return pattern * 0.5 + bayer8(pos) * 0.5;
}

// Floyd-Steinberg style error propagation simulation
float floydSteinberg(vec2 pos, vec3 color) {
  // Simulated error diffusion using neighbor sampling
  float lum = dot(color, vec3(0.299, 0.587, 0.114));

  // Sample neighbors to simulate error propagation
  vec2 offset1 = vec2(1.0, 0.0) / uResolution * uScale;
  vec2 offset2 = vec2(-1.0, 1.0) / uResolution * uScale;
  vec2 offset3 = vec2(0.0, 1.0) / uResolution * uScale;
  vec2 offset4 = vec2(1.0, 1.0) / uResolution * uScale;

  float n1 = dot(IMG_NORM_PIXEL(inputImage, vUv + offset1).rgb, vec3(0.299, 0.587, 0.114));
  float n2 = dot(IMG_NORM_PIXEL(inputImage, vUv + offset2).rgb, vec3(0.299, 0.587, 0.114));
  float n3 = dot(IMG_NORM_PIXEL(inputImage, vUv + offset3).rgb, vec3(0.299, 0.587, 0.114));
  float n4 = dot(IMG_NORM_PIXEL(inputImage, vUv + offset4).rgb, vec3(0.299, 0.587, 0.114));

  // Weighted average simulating error propagation
  float errorSim = (lum * 16.0 + n1 * 7.0 + n2 * 3.0 + n3 * 5.0 + n4 * 1.0) / 32.0;

  return fract(errorSim * 8.0 + bayer8(pos) * 0.5);
}

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 color = texColor.rgb;

  // Calculate scaled pixel position. With pixel-lock, snap to integer
  // pixel coords so the pattern doesn't shimmer when the canvas resizes.
  vec2 pixelPos = vUv * uResolution / max(uScale, 0.5);
  if (uPixelLock > 0.5) pixelPos = floor(pixelPos);

  // Get threshold based on dither type - use float comparisons for WebGL compat
  float threshold = 0.0;

  if (uType < 0.5) {
    // Classic Bayer ordered dithering
    threshold = bayer8(pixelPos);
  } else if (uType < 1.5) {
    // Blue noise dithering (film-like grain)
    threshold = blueNoise(pixelPos);
  } else if (uType < 2.5) {
    // Halftone printing style
    float lumR = color.r;
    float lumG = color.g;
    float lumB = color.b;

    // CMYK-style halftone angles
    float hR = halftone(pixelPos, 0.261799);  // 15 degrees
    float hG = halftone(pixelPos, 1.309);     // 75 degrees
    float hB = halftone(pixelPos, 0.0);       // 0 degrees

    vec3 halftoneColor = vec3(
      step(1.0 - lumR, hR),
      step(1.0 - lumG, hG),
      step(1.0 - lumB, hB)
    );

    gl_FragColor = vec4(mix(color, halftoneColor, uIntensity), texColor.a);
    return;
  } else if (uType < 3.5) {
    // Atkinson dithering (classic Mac style)
    threshold = atkinsonPattern(pixelPos);
  } else {
    // Floyd-Steinberg simulation
    threshold = floydSteinberg(pixelPos, color);
  }

  // Apply threshold with intensity control
  threshold = (threshold - 0.5) * uIntensity;

  // Quantize to color depth, then optionally snap to a fixed palette.
  float levels = pow(2.0, uColorDepth);
  vec3 dithered = color + vec3(threshold) / levels;
  dithered = floor(dithered * levels + 0.5) / levels;
  int pal = int(uPalette + 0.5);
  if (pal > 0) dithered = snapPalette(dithered, pal);

  gl_FragColor = vec4(clamp(dithered, 0.0, 1.0), texColor.a);
}
