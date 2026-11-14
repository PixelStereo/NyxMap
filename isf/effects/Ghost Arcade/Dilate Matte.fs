/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Expand alpha matte edges. Adapted from Ghost Arcade's Dilate Matte effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Keying",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "radiusValue",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 4.0,
            "DEFAULT": 2.0
        },
        {
            "NAME": "shapeValue",
            "LABEL": "Shape",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "channel",
            "LABEL": "Channel",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "mixValue",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (dilateHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uRadius (radiusValue)
#define uShape float(floor(shapeValue + 0.5))
#define uChannel (channel)
#define uMix (mixValue)

        // 1-8
         // 0=cross, 1=square, 2=circle
       // 0=luma, 1=red, 2=green, 3=blue, 4=alpha
           // 0-1

float channelVal(vec4 c, int ch) {
  if (ch == 0) return dot(c.rgb, vec3(0.299, 0.587, 0.114));
  if (ch == 1) return c.r;
  if (ch == 2) return c.g;
  if (ch == 3) return c.b;
  return c.a;
}

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uRadius < 0.5) { gl_FragColor = src; return; }

  int radius = int(clamp(uRadius, 1.0, 8.0));
  int shape = int(uShape + 0.5);
  int ch = int(uChannel + 0.5);
  vec2 texel = 1.0 / uResolution;

  vec4 maxPx = vec4(0.0);
  float maxVal = 0.0;
  for (int y = -8; y <= 8; y++) {
    if (abs(y) > radius) continue;
    for (int x = -8; x <= 8; x++) {
      if (abs(x) > radius) continue;
      if (shape == 0 && abs(x) + abs(y) > radius) continue;
      if (shape == 2 && (x*x + y*y) > radius * radius) continue;
      vec4 sCol = IMG_NORM_PIXEL(inputImage, vUv + vec2(float(x), float(y)) * texel);
      float v = channelVal(sCol, ch);
      if (v > maxVal) { maxVal = v; maxPx = sCol; }
    }
  }

  gl_FragColor = vec4(mix(src.rgb, maxPx.rgb, uMix), mix(src.a, maxPx.a, uMix));
}
