/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Multiple offset copies with progressive fade — visual delay/echo. Adapted from Ghost Arcade's Echo Repeat effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Trails",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "countValue",
            "LABEL": "Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 10.0,
            "DEFAULT": 5.0
        },
        {
            "NAME": "offsetX",
            "LABEL": "Offset X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.05
        },
        {
            "NAME": "offsetY",
            "LABEL": "Offset Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.05
        },
        {
            "NAME": "decay",
            "LABEL": "Decay",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "hueShift",
            "LABEL": "Hue Shift",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.02
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

// Ported from Ghost Arcade (echoRepeatHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uCount (countValue)
#define uOffsetX (offsetX)
#define uOffsetY (offsetY)
#define uDecay (decay)
#define uHueShift (hueShift)
#define uMode float(floor(modeValue + 0.5))

         // 1-12
       // -0.5..0.5 per-step
       // -0.5..0.5
         // 0.5-0.95 per-step
      // 0-1
          // 0=add, 1=screen, 2=replace

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
  int count = int(clamp(uCount, 1.0, 12.0));
  int mode = int(uMode + 0.5);
  vec3 acc = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec2 off = vec2(uOffsetX, uOffsetY);
  float opacity = uDecay;
  float hueOff = uHueShift;

  for (int i = 1; i < 12; i++) {
    if (i >= count) break;
    vec2 sUv = vUv - off * float(i);
    sUv = clamp(sUv, vec2(0.0), vec2(1.0));
    vec3 sCol = IMG_NORM_PIXEL(inputImage, sUv).rgb;
    if (uHueShift > 0.001) {
      vec3 hsv = rgb2hsv(sCol);
      hsv.x = fract(hsv.x + hueOff);
      sCol = hsv2rgb(hsv);
    }
    sCol *= opacity;
    if (mode == 0) acc += sCol;
    else if (mode == 1) acc = 1.0 - (1.0 - acc) * (1.0 - sCol);
    else acc = mix(acc, sCol, opacity);
    opacity *= uDecay;
    hueOff += uHueShift;
  }
  gl_FragColor = vec4(clamp(acc, 0.0, 1.0), 1.0);
}
