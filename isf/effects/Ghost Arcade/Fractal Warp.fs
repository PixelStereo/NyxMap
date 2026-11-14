/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Mandelbrot/Julia fractal with image colors mapped to iteration depth. Adapted from Ghost Arcade's Fractal Warp effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Warp",
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
            "DEFAULT": 0.5
        },
        {
            "NAME": "scale",
            "LABEL": "Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "octaves",
            "LABEL": "Octaves",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "chromatic",
            "LABEL": "Chromatic",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
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

// Ported from Ghost Arcade (fractalWarpHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uScale (scale)
#define uOctaves (octaves)
#define uSpeed (speed)
#define uChromatic (chromatic)
#define uMode float(floor(modeValue + 0.5))

        // 0-1
         // 0.5-16
       // 2-6
         // 0-3
     // 0-1
          // 0=fbm, 1=ridged, 2=hybrid

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  float a = hash21(i), b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm(vec2 p, int oct) {
  float v = 0.0; float amp = 0.5;
  for (int i = 0; i < 6; i++) {
    if (i >= oct) break;
    v += vnoise(p) * amp; p *= 2.0; amp *= 0.5;
  }
  return v;
}
float ridged(vec2 p, int oct) {
  float v = 0.0; float amp = 0.5;
  for (int i = 0; i < 6; i++) {
    if (i >= oct) break;
    v += (1.0 - abs(vnoise(p) - 0.5) * 2.0) * amp; p *= 2.0; amp *= 0.5;
  }
  return v;
}

vec2 warpOffset(vec2 uv, float t, float chromaShift) {
  int oct = int(clamp(uOctaves, 1.0, 6.0));
  int mode = int(uMode + 0.5);
  vec2 p = uv * uScale + t + chromaShift;
  float nx, ny;
  if (mode == 0) {
    nx = fbm(p, oct) - 0.5;
    ny = fbm(p + 31.7, oct) - 0.5;
  } else if (mode == 1) {
    nx = ridged(p, oct) - 0.5;
    ny = ridged(p + 31.7, oct) - 0.5;
  } else {
    nx = (fbm(p, oct) + ridged(p, oct)) * 0.5 - 0.5;
    ny = (fbm(p + 31.7, oct) + ridged(p + 31.7, oct)) * 0.5 - 0.5;
  }
  return vec2(nx, ny) * uAmount * 0.1;
}

void main() {
  float t = uTime * uSpeed * 0.2;
  vec2 baseOff = warpOffset(vUv, t, 0.0);
  vec3 col;
  if (uChromatic > 0.001) {
    vec2 offR = warpOffset(vUv, t, uChromatic * 0.5);
    vec2 offB = warpOffset(vUv, t, -uChromatic * 0.5);
    col.r = IMG_NORM_PIXEL(inputImage, vUv + offR).r;
    col.g = IMG_NORM_PIXEL(inputImage, vUv + baseOff).g;
    col.b = IMG_NORM_PIXEL(inputImage, vUv + offB).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, vUv + baseOff).rgb;
  }
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(col, src.a);
}
