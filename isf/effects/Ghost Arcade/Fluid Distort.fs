/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Iterative fluid advection — organic smoke and liquid warping. Adapted from Ghost Arcade's Fluid Distort effect.",
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
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.6,
            "DEFAULT": 0.8
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
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (fluidDistortHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uScale (scale)
#define uSpeed (speed)
#define uTurbulence (turbulence)
#define uMode float(floor(modeValue + 0.5))

        // 0-1
         // 1-16
         // 0-3
    // 0-1
          // 0=swirl, 1=push, 2=oil

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  float a = hash21(i), b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

vec2 curl(vec2 p) {
  float e = 0.05;
  float n1 = vnoise(p + vec2(0, e));
  float n2 = vnoise(p - vec2(0, e));
  float n3 = vnoise(p + vec2(e, 0));
  float n4 = vnoise(p - vec2(e, 0));
  return vec2(n1 - n2, -(n3 - n4));
}

void main() {
  vec2 p = vUv * uScale + uTime * uSpeed * 0.1;
  vec2 c = curl(p);
  if (uTurbulence > 0.001) c += curl(p * 2.0 + 13.7) * uTurbulence * 0.5;

  int mode = int(uMode + 0.5);
  vec2 off;
  if (mode == 0) off = c * uAmount * 0.1;
  else if (mode == 1) {
    vec2 d = vUv - 0.5;
    off = (c + normalize(d + 1e-6) * 0.3) * uAmount * 0.08;
  } else {
    // Oil — strong, clamped to range
    off = clamp(c, vec2(-0.5), vec2(0.5)) * uAmount * 0.15;
  }

  vec4 col = IMG_NORM_PIXEL(inputImage, vUv + off);
  gl_FragColor = col;
}
