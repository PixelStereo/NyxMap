/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Cascading echo corridor — iterative translate + scale with color shift. Adapted from Ghost Arcade's Recursive Echo effect.",
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
            "NAME": "depthValue",
            "LABEL": "Depth",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 10.0,
            "DEFAULT": 5.0
        },
        {
            "NAME": "zoomValue",
            "LABEL": "Zoom",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.9,
            "DEFAULT": 0.95
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
            "NAME": "opacityValue",
            "LABEL": "Opacity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
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
            "NAME": "offsetX",
            "LABEL": "Offset X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.02
        },
        {
            "NAME": "offsetY",
            "LABEL": "Offset Y",
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

// Ported from Ghost Arcade (recursiveEchoHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uDepth (depthValue)
#define uZoom (zoomValue)
#define uRotation (rotation)
#define uOpacity (opacityValue)
#define uHueShift (hueShift)
#define uOffsetX (offsetX)
#define uOffsetY (offsetY)
#define uMode float(floor(modeValue + 0.5))

         // 1-12
          // 0.85-1.15 per-step zoom factor
      // 0-360 per-step
       // 0-1 per-step decay
      // 0-1 per-step


          // 0=recursive zoom, 1=mirror echo, 2=spiral

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
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  int depth = int(clamp(uDepth, 1.0, 12.0));
  int mode = int(uMode + 0.5);
  vec3 acc = src;
  float opacity = uOpacity;
  float hueOff = uHueShift;
  vec2 offset = vec2(uOffsetX, uOffsetY);
  float ang = radians(uRotation);
  float ca = cos(ang), sa = sin(ang);
  float scale = uZoom;

  for (int i = 1; i < 12; i++) {
    if (i >= depth) break;
    vec2 c = vec2(0.5);
    vec2 d = (vUv - c) * pow(scale, float(i));
    d = vec2(d.x * ca - d.y * sa, d.x * sa + d.y * ca);
    vec2 sUv = c + d - offset * float(i);
    if (mode == 1) sUv = vec2(1.0 - sUv.x, sUv.y);
    else if (mode == 2) {
      // Spiral: add radial twist
      vec2 dr = sUv - 0.5;
      float r = length(dr);
      float a2 = atan(dr.y, dr.x) + r * float(i) * 0.5;
      sUv = 0.5 + vec2(cos(a2), sin(a2)) * r;
    }
    sUv = clamp(sUv, vec2(0.0), vec2(1.0));
    vec3 sCol = IMG_NORM_PIXEL(inputImage, sUv).rgb;
    if (uHueShift > 0.001) {
      vec3 hsv = rgb2hsv(sCol);
      hsv.x = fract(hsv.x + hueOff);
      sCol = hsv2rgb(hsv);
    }
    acc = mix(acc, sCol, opacity);
    opacity *= uOpacity;
    hueOff += uHueShift;
  }
  gl_FragColor = vec4(clamp(acc, 0.0, 1.0), 1.0);
}
