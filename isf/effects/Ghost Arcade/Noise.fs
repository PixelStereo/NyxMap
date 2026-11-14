/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Static or animated noise overlay. Adapted from Ghost Arcade's Noise effect.",
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
            "NAME": "noiseAmount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "typeValue",
            "LABEL": "Type",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
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
            "NAME": "scaleValue",
            "LABEL": "Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "monoValue",
            "LABEL": "Mono",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "shadowAmt",
            "LABEL": "Shadow Amt",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "midAmt",
            "LABEL": "Mid Amt",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "highAmt",
            "LABEL": "High Amt",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "animSpeed",
            "LABEL": "Anim Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (noiseHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (noiseAmount)
#define uType float(floor(typeValue + 0.5))
#define uMode float(floor(modeValue + 0.5))
#define uScale (scaleValue)
#define uMono (monoValue)
#define uShadowAmt (shadowAmt)
#define uMidAmt (midAmt)
#define uHighAmt (highAmt)
#define uAnimSpeed (animSpeed)

        // 0-1
          // 0=white, 1=blue, 2=value, 3=fbm, 4=cellular
          // 0=overlay, 1=add, 2=multiply, 3=screen, 4=replace
         // 0.5-32 noise scale
          // 0=RGB noise, 1=mono
     // 0-1
        // 0-1
       // 0-1
     // 0=static, 0-2=anim

float hash13(vec3 p) {
  p = fract(p * vec3(443.8975, 397.2973, 491.1871));
  p += dot(p, p.yzx + 19.19);
  return fract((p.x + p.y) * p.z);
}
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
  for (int i = 0; i < 5; i++) { v += vnoise(p) * amp; p *= 2.0; amp *= 0.5; }
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
      float d = dot(r, r);
      minD = min(minD, d);
    }
  }
  return sqrt(minD);
}
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 sampleNoise(vec2 p, float t) {
  int type = int(uType + 0.5);
  if (uMono > 0.5) {
    float n;
    if (type == 0) n = hash13(vec3(p, t)) - 0.5;
    else if (type == 1) {
      // Blue-style: triangular distribution from two whites
      float a = hash13(vec3(p, t));
      float b = hash13(vec3(p + 1.0, t + 0.5));
      n = (a + b) * 0.5 - 0.5;
    }
    else if (type == 2) n = vnoise(p) - 0.5;
    else if (type == 3) n = fbm(p) - 0.5;
    else n = cellular(p) - 0.5;
    return vec3(n);
  } else {
    vec3 c;
    if (type == 0) c = vec3(hash13(vec3(p, t)), hash13(vec3(p, t + 0.31)), hash13(vec3(p, t + 0.71))) - 0.5;
    else if (type == 1) {
      c = vec3(
        (hash13(vec3(p, t)) + hash13(vec3(p + 1.0, t + 0.5))) * 0.5 - 0.5,
        (hash13(vec3(p + 7.0, t + 0.31)) + hash13(vec3(p + 8.0, t + 0.81))) * 0.5 - 0.5,
        (hash13(vec3(p + 17.0, t + 0.71)) + hash13(vec3(p + 18.0, t + 0.91))) * 0.5 - 0.5
      );
    }
    else if (type == 2) c = vec3(vnoise(p), vnoise(p + 13.7), vnoise(p + 71.3)) - 0.5;
    else if (type == 3) c = vec3(fbm(p), fbm(p + 13.7), fbm(p + 71.3)) - 0.5;
    else c = vec3(cellular(p), cellular(p + 13.7), cellular(p + 71.3)) - 0.5;
    return c;
  }
}

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uAmount < 0.001) { gl_FragColor = src; return; }

  float t = (uAnimSpeed > 0.001) ? floor(uTime * uAnimSpeed * 24.0) / 24.0 : 0.0;
  vec2 p = vUv * uResolution / max(0.5, 64.0 / uScale);
  vec3 noise = sampleNoise(p, t);

  // Tonal weighting
  float l = luma(src.rgb);
  float shadowMask = 1.0 - smoothstep(0.0, 0.4, l);
  float midMask = (1.0 - abs(l - 0.5) * 2.0);
  float highMask = smoothstep(0.6, 1.0, l);
  float zoneAmp = shadowMask * uShadowAmt + midMask * uMidAmt + highMask * uHighAmt;
  noise *= uAmount * zoneAmp;

  int mode = int(uMode + 0.5);
  vec3 result;
  if (mode == 0) {
    // Overlay: noise around 0.5, blend lightly
    result = src.rgb + noise;
  } else if (mode == 1) {
    result = src.rgb + abs(noise) * sign(noise);
  } else if (mode == 2) {
    result = src.rgb * (1.0 + noise);
  } else if (mode == 3) {
    vec3 n01 = noise + 0.5;
    result = 1.0 - (1.0 - src.rgb) * (1.0 - n01 * uAmount);
  } else {
    result = noise + 0.5;
  }
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), src.a);
}
