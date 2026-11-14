/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Animated plasma color overlay. Adapted from Ghost Arcade's Plasma effect.",
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
            "NAME": "plasmaSpeed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "plasmaScale",
            "LABEL": "Scale",
            "TYPE": "float",
            "MIN": 0.1,
            "MAX": 10.0,
            "DEFAULT": 2.0
        },
        {
            "NAME": "plasmaComplexity",
            "LABEL": "Complexity",
            "TYPE": "float",
            "MIN": 1.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
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
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "blendModeValue",
            "LABEL": "Blend Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "plasmaMix",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "warpAmount",
            "LABEL": "Warp Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        }
    ]
}*/

// Ported from Ghost Arcade (plasmaShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uSpeed (plasmaSpeed)
#define uScale (plasmaScale)
#define uComplexity (plasmaComplexity)
#define uPalette float(floor(palette + 0.5))
#define uMode float(floor(modeValue + 0.5))
#define uBlendMode float(floor(blendModeValue + 0.5))
#define uMix (plasmaMix)
#define uWarpAmount (warpAmount)
#define uAudioReact 0.0
#define uAudio 0.0

vec3 rainbowPalette(float t) { return 0.5 + 0.5 * cos(6.28318 * (t + vec3(0.0, 0.33, 0.67))); }
vec3 firePalette(float t) { return vec3(smoothstep(0.0, 0.5, t), smoothstep(0.3, 0.7, t) * 0.7, smoothstep(0.7, 1.0, t) * 0.3); }
vec3 oceanPalette(float t) { return vec3(smoothstep(0.5, 1.0, t) * 0.3, smoothstep(0.2, 0.8, t) * 0.6 + 0.2, 0.4 + 0.6 * t); }
vec3 neonPalette(float t) {
  float r = sin(t * 6.28318) * 0.5 + 0.5;
  float g = sin(t * 6.28318 + 2.094) * 0.5 + 0.5;
  float b = sin(t * 6.28318 + 4.188) * 0.5 + 0.5;
  return pow(vec3(r, g, b), vec3(0.5));
}
vec3 matrixPalette(float t) { return vec3(0.0, t * 0.8 + 0.2, t * 0.3); }
vec3 lavaPalette(float t) { return vec3(smoothstep(0.0, 0.4, t) * 1.0, smoothstep(0.3, 0.85, t) * 0.6, smoothstep(0.85, 1.0, t) * 0.4); }
vec3 icePalette(float t) { return vec3(0.4 * t + smoothstep(0.7, 1.0, t) * 0.6, 0.2 + 0.6 * t, 0.5 + 0.5 * t); }
vec3 stormPalette(float t) {
  vec3 dark = vec3(0.08, 0.08, 0.12);
  vec3 mid  = vec3(0.45, 0.35, 0.55);
  vec3 hi   = vec3(0.95, 0.95, 1.00);
  return mix(mix(dark, mid, smoothstep(0.0, 0.6, t)), hi, smoothstep(0.7, 1.0, t));
}

float plasmaField(vec2 p, float t, float complexity) {
  float plasma = 0.0;
  plasma += sin(p.x * 10.0 + t);
  plasma += sin(p.y * 10.0 + t * 1.1);
  plasma += sin((p.x + p.y) * 10.0 + t * 0.5);
  plasma += sin(sqrt(p.x * p.x + p.y * p.y) * 10.0 + t * 0.7);
  if (complexity > 1.0) plasma += sin(p.x * 5.0 + sin(p.y * 3.0 + t) * 2.0);
  if (complexity > 2.0) plasma += sin(p.y * 7.0 + sin(p.x * 5.0 + t * 1.3) * 2.0);
  if (complexity > 3.0) plasma += sin(length(p - vec2(0.5 * uScale)) * 8.0 - t * 2.0);
  if (complexity > 4.0) plasma += sin(atan(p.y - 0.5 * uScale, p.x - 0.5 * uScale) * 5.0 + t);
  return plasma / (4.0 + max(complexity - 1.0, 0.0)) * 0.5 + 0.5;
}

vec3 paletteLookup(int paletteType, float t) {
  if (paletteType == 0) return rainbowPalette(t);
  if (paletteType == 1) return firePalette(t);
  if (paletteType == 2) return oceanPalette(t);
  if (paletteType == 3) return neonPalette(t);
  if (paletteType == 4) return matrixPalette(t);
  if (paletteType == 5) return lavaPalette(t);
  if (paletteType == 6) return icePalette(t);
  return stormPalette(t);
}

vec3 applyBlend(vec3 base, vec3 plasmaCol, int mode) {
  if (mode == 0) return base * plasmaCol;
  if (mode == 1) return 1.0 - (1.0 - base) * (1.0 - plasmaCol);
  if (mode == 2) return min(base + plasmaCol, vec3(1.0));
  if (mode == 3) {
    vec3 lo = 2.0 * base * plasmaCol;
    vec3 hi = 1.0 - 2.0 * (1.0 - base) * (1.0 - plasmaCol);
    return mix(lo, hi, step(0.5, base));
  }
  return plasmaCol;
}

void main() {
  float audioPunch = clamp(uAudio, 0.0, 1.5) * uAudioReact;
  float effectiveScale = uScale * (1.0 + audioPunch * 0.6);
  float t = uTime * uSpeed;
  vec2 p = vUv * effectiveScale;
  int mode = int(uMode + 0.5);

  vec2 sampleUv = vUv;
  if (mode == 1 || mode == 2) {
    vec2 warpField = vec2(
      plasmaField(p * 0.5 + vec2(0.0, 1.7), t * 0.7, max(uComplexity, 2.0)),
      plasmaField(p * 0.5 + vec2(3.1, 0.0), t * 0.9, max(uComplexity, 2.0))
    ) - 0.5;
    sampleUv += warpField * uWarpAmount * 0.18;
  }
  vec4 texColor = IMG_NORM_PIXEL(inputImage, sampleUv);

  if (mode == 1) {
    vec3 finalRgb = mix(IMG_NORM_PIXEL(inputImage, vUv).rgb, texColor.rgb, uMix);
    gl_FragColor = vec4(finalRgb, texColor.a);
    return;
  }

  float plasma = plasmaField(p, t, uComplexity);
  int paletteType = int(uPalette);
  vec3 plasmaColor = paletteLookup(paletteType, plasma);
  int blendMode = int(uBlendMode + 0.5);
  vec3 blended = applyBlend(texColor.rgb, plasmaColor, blendMode);
  vec3 finalColor = mix(texColor.rgb, blended, uMix);
  gl_FragColor = vec4(finalColor, texColor.a);
}
