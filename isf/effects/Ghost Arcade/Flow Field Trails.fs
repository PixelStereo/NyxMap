/*{
    "ISFVSN": "2",
    "DESCRIPTION": "UV streaks marched along an animated curl-noise vector field. Advect / streak / tendril modes + hue cycle. Adapted from Ghost Arcade's Flow Field Trails effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "New Hero",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "flowScale",
            "LABEL": "Flow Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "trailLength",
            "LABEL": "Trail Length",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "samplesValue",
            "LABEL": "Samples",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 48.0,
            "DEFAULT": 24.0
        },
        {
            "NAME": "speedValue",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.6,
            "DEFAULT": 0.8
        },
        {
            "NAME": "chromaSplit",
            "LABEL": "Chroma Split",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "contrast",
            "LABEL": "Contrast",
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
            "NAME": "colorCycle",
            "LABEL": "Color Cycle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (flowFieldTrailsHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uFlowScale (flowScale)
#define uTrailLength (trailLength)
#define uSamples (samplesValue)
#define uSpeed (speedValue)
#define uChromaSplit (chromaSplit)
#define uContrast (contrast)
#define uMode float(floor(modeValue + 0.5))
#define uColorCycle (colorCycle)

     // 0.5-16 noise scale
   // 0-1
       // 8-64 march steps
         // 0-3 anim speed
   // 0-1
      // 0-2
          // 0=advect, 1=streak, 2=tendril
    // 0-1 hue rotation along trail

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  float a = hash21(i), b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
vec2 curl(vec2 p, float t) {
  float e = 0.04;
  float n1 = vnoise(p + vec2(0, e) + t);
  float n2 = vnoise(p - vec2(0, e) + t);
  float n3 = vnoise(p + vec2(e, 0) + t);
  float n4 = vnoise(p - vec2(e, 0) + t);
  return vec2(n1 - n2, -(n3 - n4)) * 4.0;
}
vec3 hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  int mode = int(uMode + 0.5);
  int samples = int(clamp(uSamples, 4.0, 64.0));
  float t = uTime * uSpeed;

  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  vec2 pos = vUv;
  float stepLen = uTrailLength * 0.4 / float(samples);

  for (int i = 0; i < 64; i++) {
    if (i >= samples) break;
    float fi = float(i) / float(samples);
    vec2 v = curl(pos * uFlowScale, t * 0.1);
    if (mode == 1) v *= (1.0 + sin(fi * 6.28) * 0.5);
    else if (mode == 2) v *= (1.0 + cos(t + fi * 3.14) * 0.7);
    pos -= v * stepLen;
    vec3 sCol;
    if (uChromaSplit > 0.001) {
      sCol.r = IMG_NORM_PIXEL(inputImage, pos + v * uChromaSplit * 0.01).r;
      sCol.g = IMG_NORM_PIXEL(inputImage, pos).g;
      sCol.b = IMG_NORM_PIXEL(inputImage, pos - v * uChromaSplit * 0.01).b;
    } else {
      sCol = IMG_NORM_PIXEL(inputImage, pos).rgb;
    }
    // Optional hue cycle
    if (uColorCycle > 0.001) {
      float hueOff = fi * uColorCycle;
      sCol = mix(sCol, hsv2rgb(vec3(fract(hueOff), 1.0, max(max(sCol.r, sCol.g), sCol.b))), uColorCycle * 0.4);
    }
    float w = 1.0 - fi;
    acc += sCol * w;
    wsum += w;
  }
  vec3 result = acc / max(wsum, 0.0001);
  result = (result - 0.5) * uContrast + 0.5;
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), src.a);
}
