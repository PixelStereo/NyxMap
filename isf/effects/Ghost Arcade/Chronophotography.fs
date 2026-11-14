/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Stroboscopic multi-exposure trail effect. Linear, circular, zoom, and spiral echo trails. Adapted from Ghost Arcade's Chronophotography effect.",
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
            "NAME": "mode",
            "LABEL": "Trail Type",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "amount",
            "LABEL": "Echo Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Trail Spread",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (chronoShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode (mode)
#define uAmount (amount)
#define uAmount2 (amount2)
#define uSpeed (speed)

void main() {
  int mode = int(uMode + 0.5);
  float t = uTime * uSpeed;
  float spread = uAmount2;
  int echoes = int(uAmount * 8.0 + 2.0);
  if (echoes > 10) echoes = 10;

  vec4 color = vec4(0.0);
  float totalWeight = 0.0;

  for (int i = 0; i < 10; i++) {
    if (i >= echoes) break;
    float fi = float(i) / float(echoes - 1);
    float weight = 1.0 - fi * 0.7;
    vec2 offset = vec2(0.0);

    if (mode == 0) {
      offset = vec2(spread * (fi - 0.5) * sin(t * 0.5), spread * 0.1 * sin(t + fi * 3.0));
    } else if (mode == 1) {
      float angle = fi * 6.28 * 0.5 + t * 0.3;
      offset = vec2(cos(angle), sin(angle)) * spread * fi * 0.3;
    } else if (mode == 2) {
      vec2 centered = vUv - 0.5;
      offset = centered * fi * spread * 0.5;
    } else {
      float angle = fi * 6.28 + t * 0.5;
      float radius = fi * spread * 0.2;
      offset = vec2(cos(angle) * radius, sin(angle) * radius);
    }

    color += IMG_NORM_PIXEL(inputImage, clamp(vUv + offset, 0.0, 1.0)) * weight;
    totalWeight += weight;
  }

  gl_FragColor = color / totalWeight;
}
