/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Temporal slit-scan — each row samples different time. Adapted from Ghost Arcade's Slit-Scan effect.",
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
            "NAME": "intensity",
            "LABEL": "Intensity",
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
            "NAME": "patternValue",
            "LABEL": "Pattern",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
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
            "NAME": "chromaSplit",
            "LABEL": "Chroma Split",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (slitScanHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (intensity)
#define uMode float(floor(modeValue + 0.5))
#define uPattern (patternValue)
#define uSpeed (speed)
#define uChromaSplit (chromaSplit)

     // 0-1 displacement amount
          // 0=horizontal slits, 1=vertical, 2=radial, 3=stretch
       // 0=linear sweep, 1=sine, 2=noise
         // 0-3
   // 0-1

float hash11(float n) { return fract(sin(n) * 43758.5453); }

vec2 slitOffset(vec2 uv, float phaseShift) {
  int mode = int(uMode + 0.5);
  int pattern = int(uPattern + 0.5);
  float coord = (mode == 1) ? uv.x : uv.y;
  float t = uTime * uSpeed + phaseShift;
  float p;
  if (pattern == 0) p = coord + t * 0.3;
  else if (pattern == 1) p = sin(coord * 8.0 + t * 2.0);
  else p = (hash11(floor(coord * 50.0) + floor(t * 8.0)) - 0.5) * 2.0;
  vec2 off = vec2(0.0);
  if (mode == 0) off.x = p * uIntensity * 0.3;
  else if (mode == 1) off.y = p * uIntensity * 0.3;
  else if (mode == 2) {
    vec2 d = uv - 0.5;
    float r = length(d);
    vec2 dir = (r > 0.001) ? d / r : vec2(1.0, 0.0);
    off = dir * p * uIntensity * 0.3;
  } else {
    // Stretch: each row sampled at different progressive UV
    off.x = (uv.y - 0.5) * uIntensity * 0.5;
    off.y = sin(t + uv.x * 6.28) * uIntensity * 0.1;
  }
  return off;
}

void main() {
  vec2 baseOff = slitOffset(vUv, 0.0);
  vec3 col;
  if (uChromaSplit > 0.001) {
    vec2 offR = slitOffset(vUv, 0.3 * uChromaSplit);
    vec2 offB = slitOffset(vUv, -0.3 * uChromaSplit);
    col.r = IMG_NORM_PIXEL(inputImage, vUv + offR).r;
    col.g = IMG_NORM_PIXEL(inputImage, vUv + baseOff).g;
    col.b = IMG_NORM_PIXEL(inputImage, vUv + offB).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, vUv + baseOff).rgb;
  }
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(col, src.a);
}
