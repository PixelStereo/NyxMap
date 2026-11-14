/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Animated heat shimmer distortion. Adapted from Ghost Arcade's Heat Haze effect.",
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
            "DEFAULT": 0.4
        },
        {
            "NAME": "scaleValue",
            "LABEL": "Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 16.0,
            "DEFAULT": 8.0
        },
        {
            "NAME": "speedValue",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "directionY",
            "LABEL": "Direction Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "turbulenceValue",
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
        },
        {
            "NAME": "focusYValue",
            "LABEL": "Focus Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "focusBand",
            "LABEL": "Focus Band",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        }
    ]
}*/

// Ported from Ghost Arcade (heatHazeHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uScale (scaleValue)
#define uSpeed (speedValue)
#define uDirectionY (directionY)
#define uTurbulence (turbulenceValue)
#define uMode float(floor(modeValue + 0.5))
#define uFocusY (focusYValue)
#define uFocusBand (focusBand)

        // 0-1 displacement strength
         // 1-32 noise scale
         // 0-3 animation speed
    // -1..1 vertical bias (rising heat)
    // 0-1 fbm turbulence
          // 0=heat shimmer, 1=underwater, 2=glass distort
        // 0-1 vertical position where haze peaks
     // 0-1 band width

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

void main() {
  if (uAmount < 0.001) { gl_FragColor = IMG_NORM_PIXEL(inputImage, vUv); return; }

  int mode = int(uMode + 0.5);
  vec2 p = vUv * uScale + vec2(0.0, -uTime * uSpeed * 0.5);
  p.y += uDirectionY * uTime * uSpeed * 0.3;

  float nx, ny;
  if (uTurbulence > 0.001) {
    nx = fbm(p) - 0.5;
    ny = fbm(p + vec2(123.4, 56.7)) - 0.5;
  } else {
    nx = vnoise(p) - 0.5;
    ny = vnoise(p + vec2(123.4, 56.7)) - 0.5;
  }

  float strength = uAmount * 0.05;
  if (mode == 0) {
    // Heat shimmer — mostly horizontal, falls off above focusY
    ny *= 0.4;
    float bandMask = exp(-pow((vUv.y - uFocusY) / max(0.05, uFocusBand), 2.0));
    strength *= bandMask;
  } else if (mode == 1) {
    // Underwater — both directions, sinusoidal modulation
    float wave = sin(uTime * uSpeed + vUv.y * 8.0) * 0.5;
    nx *= (1.0 + wave * 0.5);
    ny *= (1.0 + wave * 0.5);
  } else {
    // Glass distort — strong, both directions
    strength *= 1.5;
  }

  vec2 off = vec2(nx, ny) * strength;
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv + off);
  gl_FragColor = src;
}
