/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Animated noise-based UV displacement. Adapted from Ghost Arcade's Displacement effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Distort",
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
            "DEFAULT": 0.4
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
            "NAME": "speed",
            "LABEL": "Speed",
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
        },
        {
            "NAME": "turbulence",
            "LABEL": "Turbulence",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "chromatic",
            "LABEL": "Chromatic",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (displacementHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uScale (scale)
#define uSpeed (speed)
#define uMode float(floor(modeValue + 0.5))
#define uTurbulence (turbulence)
#define uChromatic (chromatic)

        // 0-1
         // 1-32
         // 0-3
          // 0=fbm, 1=cellular, 2=sine grid, 3=ripple
    // 0-1
     // 0-1

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  float a = hash21(i), b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm(vec2 p) {
  float v = 0.0; float amp = 0.5;
  for (int i = 0; i < 4; i++) { v += vnoise(p) * amp; p *= 2.0; amp *= 0.5; }
  return v;
}
float cellular(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  float minD = 1.0;
  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      vec2 g = vec2(float(x), float(y));
      vec2 o = vec2(hash21(i + g), hash21(i + g + 13.0));
      vec2 r = g + o - f;
      minD = min(minD, dot(r, r));
    }
  }
  return sqrt(minD);
}

vec2 dispOffset(vec2 uv, float t) {
  int mode = int(uMode + 0.5);
  vec2 p = uv * uScale + vec2(t, t * 0.7);
  float nx, ny;
  if (mode == 0) {
    nx = (uTurbulence > 0.5 ? fbm(p) : vnoise(p)) - 0.5;
    ny = (uTurbulence > 0.5 ? fbm(p + 71.3) : vnoise(p + 71.3)) - 0.5;
  } else if (mode == 1) {
    nx = cellular(p) - 0.5;
    ny = cellular(p + 71.3) - 0.5;
  } else if (mode == 2) {
    nx = sin(uv.y * uScale * 6.283 + t * 2.0);
    ny = sin(uv.x * uScale * 6.283 + t * 2.0);
  } else {
    vec2 d = uv - 0.5;
    float r = length(d);
    float ripple = sin(r * uScale * 6.283 - t * 3.0);
    vec2 dir = (r > 0.001) ? d / r : vec2(1.0, 0.0);
    nx = dir.x * ripple;
    ny = dir.y * ripple;
  }
  return vec2(nx, ny) * uAmount * 0.05;
}

void main() {
  float t = uTime * uSpeed;
  vec2 baseOff = dispOffset(vUv, t);
  vec3 col;
  if (uChromatic > 0.001) {
    vec2 offR = dispOffset(vUv, t + 0.3 * uChromatic);
    vec2 offB = dispOffset(vUv, t - 0.3 * uChromatic);
    col.r = IMG_NORM_PIXEL(inputImage, vUv + offR).r;
    col.g = IMG_NORM_PIXEL(inputImage, vUv + baseOff).g;
    col.b = IMG_NORM_PIXEL(inputImage, vUv + offB).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, vUv + baseOff).rgb;
  }
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(col, src.a);
}
