/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Soft watercolor wash with edge darkening. Adapted from Ghost Arcade's Watercolor effect.",
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
            "NAME": "bleedValue",
            "LABEL": "Bleed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "edgeDarken",
            "LABEL": "Edge Darken",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "paperTexture",
            "LABEL": "Paper Texture",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "paperScale",
            "LABEL": "Paper Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 16.0,
            "DEFAULT": 8.0
        },
        {
            "NAME": "wetness",
            "LABEL": "Wetness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "granulationValue",
            "LABEL": "Granulation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "paperHue",
            "LABEL": "Paper Hue",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (watercolorHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uBleed (bleedValue)
#define uEdgeDarken (edgeDarken)
#define uPaperTexture (paperTexture)
#define uPaperScale (paperScale)
#define uWetness (wetness)
#define uGranulation (granulationValue)
#define uPaperHue (paperHue)

         // 0-1 pigment bleed radius
    // 0-1 sobel-driven edge darkening
  // 0-1 paper noise strength
    // 1-32 paper noise scale
       // 0-1 colour saturation boost (wet pigment)
   // 0-1 pigment granulation noise
      // 0=cream, 1=cool grey, 2=tea-stain

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  float a = hash21(i), b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 paperColor(float kind) {
  if (kind < 0.5) return vec3(0.96, 0.93, 0.86); // cream
  if (kind < 1.5) return vec3(0.88, 0.90, 0.93); // cool grey
  return vec3(0.82, 0.72, 0.55); // tea
}

void main() {
  vec2 texel = 1.0 / uResolution;

  // ── Pigment bleed: 9-tap blur with radius scaled by uBleed ──
  float r = uBleed * 6.0 + 1.0;
  vec3 sum = vec3(0.0);
  float weight = 0.0;
  for (int y = -2; y <= 2; y++) {
    for (int x = -2; x <= 2; x++) {
      vec2 off = vec2(float(x), float(y)) * texel * r;
      float w = exp(-dot(off, off) * 100.0);
      sum += IMG_NORM_PIXEL(inputImage, vUv + off).rgb * w;
      weight += w;
    }
  }
  vec3 bled = sum / weight;

  // ── Edge darken (sobel on luma) ──
  float l00 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(-1.0, -1.0)).rgb);
  float l10 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 0.0, -1.0)).rgb);
  float l20 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 1.0, -1.0)).rgb);
  float l01 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(-1.0,  0.0)).rgb);
  float l21 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 1.0,  0.0)).rgb);
  float l02 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(-1.0,  1.0)).rgb);
  float l12 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 0.0,  1.0)).rgb);
  float l22 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 1.0,  1.0)).rgb);
  float gxL = (l20 + 2.0 * l21 + l22) - (l00 + 2.0 * l01 + l02);
  float gyL = (l02 + 2.0 * l12 + l22) - (l00 + 2.0 * l10 + l20);
  float edge = clamp(length(vec2(gxL, gyL)), 0.0, 1.0);
  bled = mix(bled, bled * (1.0 - edge), uEdgeDarken);

  // ── Wetness (saturation boost) ──
  if (uWetness > 0.001) {
    float lum = luma(bled);
    bled = mix(vec3(lum), bled, 1.0 + uWetness * 0.5);
  }

  // ── Pigment granulation ──
  if (uGranulation > 0.001) {
    float gran = vnoise(vUv * uResolution * 0.6 + uTime * 0.05) - 0.5;
    bled += vec3(gran) * uGranulation * 0.15;
  }

  // ── Paper texture composite ──
  float paperN = vnoise(vUv * uPaperScale * 16.0) * 0.5 + vnoise(vUv * uPaperScale * 32.0) * 0.5;
  vec3 paper = paperColor(uPaperHue) * (0.85 + paperN * 0.3);
  vec3 result = bled * mix(vec3(1.0), paper, uPaperTexture);

  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), src.a);
}
