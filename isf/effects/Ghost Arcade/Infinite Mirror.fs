/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Recursive mirror room with chromatic reflections fading into infinity. Adapted from Ghost Arcade's Infinite Mirror effect.",
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
            "NAME": "iterations",
            "LABEL": "Iterations",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 10.0,
            "DEFAULT": 5.0
        },
        {
            "NAME": "shrinkValue",
            "LABEL": "Shrink",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.6,
            "DEFAULT": 0.8
        },
        {
            "NAME": "rotation",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 10.0,
            "DEFAULT": 5.0
        },
        {
            "NAME": "tintFade",
            "LABEL": "Tint Fade",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "hueShift",
            "LABEL": "Hue Shift",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.05
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
            "NAME": "offsetX",
            "LABEL": "Offset X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "offsetY",
            "LABEL": "Offset Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        }
    ]
}*/

// Ported from Ghost Arcade (infiniteMirrorHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIterations (iterations)
#define uShrink (shrinkValue)
#define uRotation (rotation)
#define uTintFade (tintFade)
#define uHueShift (hueShift)
#define uMode float(floor(modeValue + 0.5))
#define uOffsetX (offsetX)
#define uOffsetY (offsetY)

    // 1-12
        // 0.5-0.95 per-iter shrink
      // 0-360 per-iter
      // 0-1
      // 0-1 per-iter
          // 0=center, 1=offset

vec3 hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
vec3 rgb2hsv(vec3 c) {
  vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}

void main() {
  int iters = int(clamp(uIterations, 1.0, 12.0));
  int mode = int(uMode + 0.5);
  vec2 c = (mode == 1) ? vec2(uOffsetX, uOffsetY) : vec2(0.5);
  vec3 acc = vec3(0.0);
  float weight = 0.0;
  float ang = radians(uRotation);
  float ca = cos(ang), sa = sin(ang);
  float scale = 1.0;
  float hueOff = 0.0;
  float tint = 1.0;

  for (int i = 0; i < 12; i++) {
    if (i >= iters) break;
    vec2 d = (vUv - c) / scale;
    d = vec2(d.x * ca - d.y * sa, d.x * sa + d.y * ca);
    vec2 sUv = c + d;
    sUv = clamp(sUv, vec2(0.0), vec2(1.0));
    vec3 sCol = IMG_NORM_PIXEL(inputImage, sUv).rgb;
    if (uHueShift > 0.001) {
      vec3 hsv = rgb2hsv(sCol);
      hsv.x = fract(hsv.x + hueOff);
      sCol = hsv2rgb(hsv);
    }
    acc += sCol * tint;
    weight += tint;
    scale *= uShrink;
    hueOff += uHueShift;
    tint *= 1.0 - uTintFade * 0.5;
  }
  vec3 result = acc / max(weight, 0.0001);
  gl_FragColor = vec4(result, 1.0);
}
