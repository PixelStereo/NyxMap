/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Photographic film grain texture. Adapted from Ghost Arcade's Film Grain effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Generate & Texture",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "amount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "sizeValue",
            "LABEL": "Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "shadowGrain",
            "LABEL": "Shadow Grain",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "midGrain",
            "LABEL": "Mid Grain",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "highGrain",
            "LABEL": "High Grain",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "mono",
            "LABEL": "Mono",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "stockValue",
            "LABEL": "Stock",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "colorJitter",
            "LABEL": "Color Jitter",
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
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (filmGrainHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uSize (sizeValue)
#define uShadowGrain (shadowGrain)
#define uMidGrain (midGrain)
#define uHighGrain (highGrain)
#define uMono (mono)
#define uStock (stockValue)
#define uColorJitter (colorJitter)
#define uAnimSpeed (animSpeed)

        // 0-1 grain strength
          // 0.5-4 grain size (px)
   // 0-1 grain in shadows
      // 0-1 grain in midtones
     // 0-1 grain in highlights
          // 0-1 monochrome grain (vs RGB)
         // 0=fine, 1=35mm, 2=16mm, 3=Super8
   // 0-1 chroma noise

     // 0-1 anim speed (1 = per-frame)

float hash13(vec3 p) {
  p = fract(p * vec3(443.8975, 397.2973, 491.1871));
  p += dot(p, p.yzx + 19.19);
  return fract((p.x + p.y) * p.z);
}
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uAmount < 0.001) { gl_FragColor = src; return; }

  int stock = int(uStock + 0.5);
  float grainScale = (stock == 0) ? 1.5 : (stock == 1) ? 1.0 : (stock == 2) ? 0.6 : 0.35;
  grainScale *= uSize;
  vec2 gPos = vUv * uResolution / max(0.5, grainScale);
  float t = floor(uTime * uAnimSpeed * 24.0) / 24.0;

  float n = hash13(vec3(gPos, t));
  vec3 noiseRgb;
  if (uMono > 0.5) {
    noiseRgb = vec3(n - 0.5);
  } else {
    float r = hash13(vec3(gPos, t + 0.1));
    float g = hash13(vec3(gPos, t + 0.3));
    float b = hash13(vec3(gPos, t + 0.7));
    noiseRgb = vec3(r - 0.5, g - 0.5, b - 0.5);
  }

  // Tonal response — different grain in shadows / mids / highs
  float l = luma(src.rgb);
  float shadowMask = 1.0 - smoothstep(0.0, 0.4, l);
  float midMask = (1.0 - abs(l - 0.5) * 2.0);
  float highMask = smoothstep(0.6, 1.0, l);
  float zoneAmp = shadowMask * uShadowGrain + midMask * uMidGrain + highMask * uHighGrain;

  float chromaJit = 0.0;
  if (uColorJitter > 0.001) {
    chromaJit = (hash13(vec3(gPos, t + 0.5)) - 0.5) * uColorJitter;
  }

  vec3 grain = noiseRgb * uAmount * zoneAmp + vec3(chromaJit, -chromaJit, chromaJit * 0.5) * 0.2;
  vec3 result = src.rgb + grain;
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), src.a);
}
