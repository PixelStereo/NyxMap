/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Separate and offset RGB channels. Adapted from Ghost Arcade's RGB Shift effect.",
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
            "NAME": "rgbShiftAmount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 0.05,
            "DEFAULT": 0.01
        },
        {
            "NAME": "rgbShiftAngle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 360.0,
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
            "NAME": "centerX",
            "LABEL": "Center X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "centerY",
            "LABEL": "Center Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "prismSpread",
            "LABEL": "Prism Spread",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (rgbShiftShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (rgbShiftAmount)
#define uAngle (rgbShiftAngle)
#define uMode float(floor(modeValue + 0.5))
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uPrismSpread (prismSpread)

     // 0-50 pixels
      // 0-360 degrees (for directional mode)
       // 0=directional, 1=radial, 2=prism, 3=luma-dep, 4=edge-only
    // 0-1 — center for radial/prism

// 0-2 — extra hue spread in prism mode

float lumaRGB(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  int mode = int(uMode + 0.5);
  vec4 base = IMG_NORM_PIXEL(inputImage, vUv);

  // Per-pixel shift direction & magnitude.
  vec2 dir = vec2(0.0);
  float amt = uAmount;

  if (mode == 0) {
    // Directional (legacy).
    float angle = uAngle * 3.14159 / 180.0;
    dir = vec2(cos(angle), sin(angle));
  } else if (mode == 1 || mode == 2) {
    // Radial / prism — direction = away from center, magnitude scales
    // with distance from center (lens-fringe behavior).
    vec2 center = vec2(uCenterX, uCenterY);
    vec2 d = vUv - center;
    float dist = length(d);
    dir = (dist > 1e-5) ? d / dist : vec2(1.0, 0.0);
    amt *= dist * 2.0;
  } else if (mode == 3) {
    // Luma-dependent — only shift on bright pixels (mimics lens flare).
    float lum = lumaRGB(base.rgb);
    float angle = uAngle * 3.14159 / 180.0;
    dir = vec2(cos(angle), sin(angle));
    amt *= smoothstep(0.4, 0.95, lum);
  } else {
    // Edge-only — shift only where image gradients are high.
    float angle = uAngle * 3.14159 / 180.0;
    dir = vec2(cos(angle), sin(angle));
    vec2 t = 2.0 / uResolution;
    float gx = lumaRGB(IMG_NORM_PIXEL(inputImage, vUv + vec2(t.x, 0.0)).rgb)
             - lumaRGB(IMG_NORM_PIXEL(inputImage, vUv - vec2(t.x, 0.0)).rgb);
    float gy = lumaRGB(IMG_NORM_PIXEL(inputImage, vUv + vec2(0.0, t.y)).rgb)
             - lumaRGB(IMG_NORM_PIXEL(inputImage, vUv - vec2(0.0, t.y)).rgb);
    float edge = clamp(length(vec2(gx, gy)) * 6.0, 0.0, 1.0);
    amt *= edge;
  }

  vec2 shift = dir * amt / uResolution;

  vec4 color;
  if (mode == 2) {
    // Prism: spread R/G/B across a wider arc, shifted toward rainbow
    // dispersion. uPrismSpread bumps the per-channel offset asymmetry.
    float k = 1.0 + uPrismSpread;
    color.r = IMG_NORM_PIXEL(inputImage, vUv + shift * (1.0 + 0.4 * k)).r;
    color.g = IMG_NORM_PIXEL(inputImage, vUv + shift * 0.0).g;
    color.b = IMG_NORM_PIXEL(inputImage, vUv - shift * (1.0 + 0.4 * k)).b;
  } else {
    color.r = IMG_NORM_PIXEL(inputImage, vUv + shift).r;
    color.g = IMG_NORM_PIXEL(inputImage, vUv).g;
    color.b = IMG_NORM_PIXEL(inputImage, vUv - shift).b;
  }
  color.a = base.a;

  gl_FragColor = color;
}
