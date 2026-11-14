/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Raymarched crystal gem refracting image with chromatic dispersion. Adapted from Ghost Arcade's Crystal Refract effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Depth",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "scale",
            "LABEL": "Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 12.0,
            "DEFAULT": 6.0
        },
        {
            "NAME": "refraction",
            "LABEL": "Refraction",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "sparkle",
            "LABEL": "Sparkle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "edgeGlow",
            "LABEL": "Edge Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
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
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (crystalRefractHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uScale (scale)
#define uRefraction (refraction)
#define uSparkle (sparkle)
#define uEdgeGlow (edgeGlow)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uMode float(floor(modeValue + 0.5))

         // 1-16
    // 0-1 displacement
       // 0-1
      // 0-1



          // 0=voronoi, 1=hex

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
  int mode = int(uMode + 0.5);
  vec2 p = vUv * uScale;
  vec2 cellCenter; float minD = 9.0; float secondD = 9.0;

  if (mode == 0) {
    vec2 i = floor(p), f = fract(p);
    vec2 best = vec2(0.0);
    for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
        vec2 g = vec2(float(x), float(y));
        vec2 o = vec2(hash21(i + g), hash21(i + g + 13.7));
        // Slight time wobble
        o = 0.5 + 0.45 * sin(uTime * 0.4 + 6.28 * o);
        vec2 r = g + o - f;
        float d = dot(r, r);
        if (d < minD) { secondD = minD; minD = d; best = i + g; }
        else if (d < secondD) secondD = d;
      }
    }
    cellCenter = (best + 0.5) / uScale;
  } else {
    // Hex
    vec2 q = vec2(p.x * 1.1547, p.y); q.x += 0.5 * floor(q.y);
    vec2 i = floor(q);
    cellCenter = (vec2(i.x - floor(i.y / 2.0), i.y) + 0.5) / vec2(uScale * 1.1547, uScale);
    vec2 f = fract(q) - 0.5;
    minD = dot(f, f);
    secondD = minD + 0.3;
  }

  // Refraction: displace toward cell center
  vec2 dir = vUv - cellCenter;
  vec2 sUv = vUv - dir * uRefraction;
  sUv = clamp(sUv, vec2(0.0), vec2(1.0));
  vec3 col = IMG_NORM_PIXEL(inputImage, sUv).rgb;

  // Edge glow (where two cells meet)
  float edge = smoothstep(0.04, 0.0, sqrt(secondD) - sqrt(minD));
  col += vec3(uTintR, uTintG, uTintB) * edge * uEdgeGlow;

  // Sparkle: bright dot at random cell centers
  if (uSparkle > 0.001) {
    float dCenter = length(vUv - cellCenter);
    float spark = step(1.0 - uSparkle * 0.3, hash21(floor(cellCenter * 100.0)));
    col += vec3(1.0) * smoothstep(0.04, 0.0, dCenter) * spark * uSparkle;
  }
  gl_FragColor = vec4(col, 1.0);
}
