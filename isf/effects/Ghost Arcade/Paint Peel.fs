/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Edges curl/flake away based on noise + luma. fBm / cellular / cracks patterns with curl shading. Adapted from Ghost Arcade's Paint Peel effect.",
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
            "NAME": "amount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "scaleValue",
            "LABEL": "Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "lumaBias",
            "LABEL": "Luma Bias",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "curlValue",
            "LABEL": "Curl",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "shadow",
            "LABEL": "Shadow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "bgR",
            "LABEL": "Bg R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.15
        },
        {
            "NAME": "bgG",
            "LABEL": "Bg G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.13
        },
        {
            "NAME": "bgB",
            "LABEL": "Bg B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
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

// Ported from Ghost Arcade (paintPeelHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uScale (scaleValue)
#define uLumaBias (lumaBias)
#define uCurl (curlValue)
#define uShadow (shadow)
#define uBgR (bgR)
#define uBgG (bgG)
#define uBgB (bgB)
#define uMode float(floor(modeValue + 0.5))

        // 0-1 peel progress
         // 1-16 noise scale
      // 0-1 (peel darks vs lights)
          // 0-1 curl shading
        // 0-1 dark crack edge



          // 0=fbm, 1=cellular, 2=cracks

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
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec2 p = vUv * uScale;
  int mode = int(uMode + 0.5);
  float field;
  if (mode == 0) field = fbm(p + uTime * 0.05);
  else if (mode == 1) field = cellular(p);
  else {
    // Cracks: combine noises into thin lines
    float n1 = fbm(p);
    float n2 = fbm(p + 13.7);
    field = abs(n1 - n2) * 4.0;
  }
  float l = luma(src);
  float lumaWeight = mix(l, 1.0 - l, uLumaBias);
  float peel = step(field, uAmount * lumaWeight + 0.1);

  // Curl shading: gradient of field acts as fake highlight
  float lift = smoothstep(uAmount - 0.05, uAmount + 0.05, field) * uCurl;
  vec3 above = src * (1.0 - lift * 0.4);

  // Shadow at peel boundary
  float shadowEdge = smoothstep(0.05, 0.0, abs(field - uAmount));
  above *= 1.0 - shadowEdge * uShadow * 0.6;

  vec3 below = vec3(uBgR, uBgG, uBgB);
  vec3 result = mix(above, below, peel);
  gl_FragColor = vec4(result, 1.0);
}
