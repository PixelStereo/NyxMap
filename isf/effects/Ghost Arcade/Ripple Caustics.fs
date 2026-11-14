/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Animated underwater caustic light patterns. Adapted from Ghost Arcade's Ripple Caustics effect.",
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
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "scale",
            "LABEL": "Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 16.0,
            "DEFAULT": 8.0
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "refraction",
            "LABEL": "Refraction",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
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
        }
    ]
}*/

// Ported from Ghost Arcade (rippleCausticsHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (intensity)
#define uScale (scale)
#define uSpeed (speed)
#define uRefraction (refraction)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uMode float(floor(modeValue + 0.5))

     // 0-2
         // 1-32
         // 0-3
    // 0-1 distort source



          // 0=overlay, 1=add, 2=screen

// Caustic via Voronoi ridge
float caustic(vec2 p, float t) {
  vec2 i = floor(p), f = fract(p);
  float minD1 = 9.0; float minD2 = 9.0;
  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      vec2 g = vec2(float(x), float(y));
      vec2 o = vec2(
        fract(sin(dot(i + g, vec2(127.1, 311.7))) * 43758.5453),
        fract(sin(dot(i + g, vec2(269.5, 183.3))) * 43758.5453)
      );
      o = 0.5 + 0.5 * sin(t + 6.28 * o);
      vec2 r = g + o - f;
      float d = dot(r, r);
      if (d < minD1) { minD2 = minD1; minD1 = d; }
      else if (d < minD2) minD2 = d;
    }
  }
  return sqrt(minD2) - sqrt(minD1);
}

void main() {
  vec2 p = vUv * uScale;
  float t = uTime * uSpeed;
  float c1 = caustic(p, t);
  float c2 = caustic(p + 17.3, t * 1.3 + 1.7);
  float c = pow(min(c1, c2), 1.5) * uIntensity;

  vec2 sUv = vUv;
  if (uRefraction > 0.001) {
    sUv += vec2(c1 - c2, c2 - c1) * uRefraction * 0.04;
  }
  vec3 src = IMG_NORM_PIXEL(inputImage, sUv).rgb;

  vec3 caustColor = vec3(uTintR, uTintG, uTintB) * c;
  int mode = int(uMode + 0.5);
  vec3 result;
  if (mode == 0) result = src + caustColor;
  else if (mode == 1) result = src + caustColor * 1.5;
  else result = 1.0 - (1.0 - src) * (1.0 - caustColor);

  gl_FragColor = vec4(result, IMG_NORM_PIXEL(inputImage, vUv).a);
}
