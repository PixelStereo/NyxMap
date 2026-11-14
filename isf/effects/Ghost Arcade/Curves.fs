/*{
    "ISFVSN": "2",
    "DESCRIPTION": "RGB gamma curve adjustments. Adapted from Ghost Arcade's Curves effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Color",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "contrastValue",
            "LABEL": "Contrast",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "toe",
            "LABEL": "Toe",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "shoulder",
            "LABEL": "Shoulder",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "blackCrush",
            "LABEL": "Black Crush",
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

// Ported from Ghost Arcade (curvesHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uContrast (contrastValue)
#define uToe (toe)
#define uShoulder (shoulder)
#define uBlackCrush (blackCrush)
#define uMix (mixValue)

    // 0-1 — S-curve strength (0 = linear, 1 = strong S)
         // 0-1 — lift the dark end (anti-crush)
    // 0-1 — soften the bright end (anti-blow-out)
  // 0-1 — crush pixels below threshold to true black

// Hermite-style S curve centered at 0.5.
float sCurve(float x, float strength) {
  float t = smoothstep(0.0, 1.0, x);
  return mix(x, t * t * (3.0 - 2.0 * t), strength);
}

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 src = texColor.rgb;

  // Pre-process: black crush — anything below threshold goes to 0.
  vec3 crushed = src;
  if (uBlackCrush > 0.001) {
    float th = uBlackCrush * 0.15;
    crushed = max(crushed - vec3(th), vec3(0.0)) / max(1.0 - th, 0.001);
  }

  // Apply S-curve per channel for contrast.
  vec3 sShaped;
  sShaped.r = sCurve(crushed.r, uContrast);
  sShaped.g = sCurve(crushed.g, uContrast);
  sShaped.b = sCurve(crushed.b, uContrast);

  // Toe lift — raise the dark pixels back up.
  vec3 toed = mix(sShaped, sShaped + (1.0 - sShaped) * 0.0, 0.0); // placeholder identity
  // Toe = subtle gamma lift on shadows only.
  if (uToe > 0.001) {
    float toeAmt = uToe * 0.5;
    sShaped = pow(sShaped, vec3(1.0 - toeAmt));
  }

  // Shoulder — soft compress highlights.
  if (uShoulder > 0.001) {
    vec3 sho = 1.0 - exp(-sShaped * (1.0 + uShoulder * 2.0));
    sShaped = mix(sShaped, sho, uShoulder);
  }

  gl_FragColor = vec4(mix(src, sShaped, uMix), texColor.a);
}
