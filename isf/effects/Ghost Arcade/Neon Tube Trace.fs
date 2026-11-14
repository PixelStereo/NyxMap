/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Detect edges and convert to glowing animated neon tubes with marching chase + flicker. Adapted from Ghost Arcade's Neon Tube Trace effect.",
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
            "NAME": "edgeThreshold",
            "LABEL": "Edge Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.15
        },
        {
            "NAME": "tubeWidth",
            "LABEL": "Tube Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.5
        },
        {
            "NAME": "glowValue",
            "LABEL": "Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "glowRadius",
            "LABEL": "Glow Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 12.0,
            "DEFAULT": 6.0
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "chaseValue",
            "LABEL": "Chase",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "chaseSpeed",
            "LABEL": "Chase Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "flickerValue",
            "LABEL": "Flicker",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "bgValue",
            "LABEL": "Bg",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 4.0,
            "DEFAULT": 2.0
        }
    ]
}*/

// Ported from Ghost Arcade (neonTubeTraceHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uEdgeThreshold (edgeThreshold)
#define uTubeWidth (tubeWidth)
#define uGlow (glowValue)
#define uGlowRadius (glowRadius)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uChase (chaseValue)
#define uChaseSpeed (chaseSpeed)
#define uFlicker (flickerValue)
#define uBg (bgValue)

 // 0-1
     // 0.5-4
          // 0-2 glow intensity
    // 1-12 px



         // 0-1 marching-light along tube
    // 0-3
       // 0-1 random tube flicker
            // 0=black, 1=keep source, 2=darkened source

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float edgeStrength(vec2 uv, vec2 texel) {
  float l00 = luma(IMG_NORM_PIXEL(inputImage, uv + texel * vec2(-1, -1)).rgb);
  float l10 = luma(IMG_NORM_PIXEL(inputImage, uv + texel * vec2( 0, -1)).rgb);
  float l20 = luma(IMG_NORM_PIXEL(inputImage, uv + texel * vec2( 1, -1)).rgb);
  float l01 = luma(IMG_NORM_PIXEL(inputImage, uv + texel * vec2(-1,  0)).rgb);
  float l21 = luma(IMG_NORM_PIXEL(inputImage, uv + texel * vec2( 1,  0)).rgb);
  float l02 = luma(IMG_NORM_PIXEL(inputImage, uv + texel * vec2(-1,  1)).rgb);
  float l12 = luma(IMG_NORM_PIXEL(inputImage, uv + texel * vec2( 0,  1)).rgb);
  float l22 = luma(IMG_NORM_PIXEL(inputImage, uv + texel * vec2( 1,  1)).rgb);
  float gx = (l20 + 2.0 * l21 + l22) - (l00 + 2.0 * l01 + l02);
  float gy = (l02 + 2.0 * l12 + l22) - (l00 + 2.0 * l10 + l20);
  return length(vec2(gx, gy));
}

void main() {
  vec2 texel = 1.0 / uResolution;
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  float e = edgeStrength(vUv, texel);
  float tube = smoothstep(uEdgeThreshold, uEdgeThreshold + 0.05 * uTubeWidth, e);

  // Glow halo (sample_ edges in neighbourhood)
  float halo = 0.0;
  if (uGlow > 0.001) {
    float r = uGlowRadius;
    for (int y = -3; y <= 3; y++) {
      for (int x = -3; x <= 3; x++) {
        vec2 off = vec2(float(x), float(y)) * texel * r * 0.4;
        float ee = edgeStrength(vUv + off, texel);
        float w = exp(-(float(x*x + y*y)) / (2.0 * 4.0));
        halo += smoothstep(uEdgeThreshold, uEdgeThreshold + 0.1, ee) * w;
      }
    }
    halo *= uGlow / 16.0;
  }

  // Marching chase
  if (uChase > 0.001 && tube > 0.5) {
    float chase = sin(vUv.x * 60.0 + uTime * uChaseSpeed * 4.0) * 0.5 + 0.5;
    tube *= mix(0.5, 1.0 + chase * 0.6, uChase);
  }

  // Flicker
  if (uFlicker > 0.001) {
    float f = step(0.92, hash21(vec2(floor(uTime * 12.0))));
    tube *= 1.0 - f * uFlicker * 0.4;
  }

  vec3 tint = vec3(uTintR, uTintG, uTintB);
  vec3 neon = tint * (tube * 1.8 + halo);

  int bg = int(uBg + 0.5);
  vec3 baseColor;
  if (bg == 0) baseColor = vec3(0.0);
  else if (bg == 1) baseColor = src;
  else baseColor = src * 0.25;

  vec3 result = 1.0 - (1.0 - baseColor) * (1.0 - neon);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
