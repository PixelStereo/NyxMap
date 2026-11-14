/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Photographic exposure adjustment. Adapted from Ghost Arcade's Exposure effect.",
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
            "NAME": "exposureValue",
            "LABEL": "Exposure",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "rollOff",
            "LABEL": "Roll Off",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "highlightProtect",
            "LABEL": "Highlight Protect",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (exposureHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uExposure (exposureValue)
#define uRollOff (rollOff)
#define uHighlightProtect (highlightProtect)

          // -2 to +2 stops (multiplied into the linear gain)
           // 0-1 highlight shoulder softness (0 = hard clip, 1 = very soft)
  // 0-1 — reduce exposure gain on already-bright pixels

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 src = texColor.rgb;

  // Photographic exposure: each stop = 2× linear gain.
  float gain = pow(2.0, uExposure);

  // Highlight protect — reduce gain locally on bright pixels. Per-pixel
  // luminance feeds a smoothstep so dark/mid pixels get full gain and
  // bright pixels get less. uHighlightProtect=1 means highlights are
  // mostly preserved; =0 means uniform gain across the image.
  float lum = dot(src, vec3(0.299, 0.587, 0.114));
  float protect = 1.0 - uHighlightProtect * smoothstep(0.5, 1.0, lum);
  vec3 lifted = src * gain * protect;

  // Highlight roll-off — soft-knee compress the top end so values
  // above 1 fold back toward 1 instead of clipping. uRollOff=0 leaves
  // the legacy hard clip; uRollOff=1 gives a very soft shoulder.
  if (uRollOff > 0.001) {
    float k = mix(8.0, 1.0, uRollOff); // higher k = sharper knee
    // Reinhard-style: x / (1 + x/k) — smooth asymptote toward 1.
    lifted = lifted / (1.0 + max(lifted - 0.0, 0.0) / k);
  }

  gl_FragColor = vec4(clamp(lifted, 0.0, 1.0), texColor.a);
}
