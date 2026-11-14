/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Fade edges to transparent with adjustable shape. Adapted from Ghost Arcade's Vignette effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Masking",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "vignetteSize",
            "LABEL": "Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "vignetteSoftness",
            "LABEL": "Softness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "vignetteRoundness",
            "LABEL": "Roundness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "shapeValue",
            "LABEL": "Shape",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "aspectValue",
            "LABEL": "Aspect",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "centerX",
            "LABEL": "Center X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "centerY",
            "LABEL": "Center Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "colorR",
            "LABEL": "Color R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "colorG",
            "LABEL": "Color G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "colorB",
            "LABEL": "Color B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "tintAmount",
            "LABEL": "Tint Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "breathingValue",
            "LABEL": "Breathing",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "breathSpeed",
            "LABEL": "Breath Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        }
    ]
}*/

// Ported from Ghost Arcade (vignetteShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uSize (vignetteSize)
#define uSoftness (vignetteSoftness)
#define uRoundness (vignetteRoundness)
#define uShape float(floor(shapeValue + 0.5))
#define uAspect (aspectValue)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uColorR (colorR)
#define uColorG (colorG)
#define uColorB (colorB)
#define uTintAmount (tintAmount)
#define uBreathing (breathingValue)
#define uBreathSpeed (breathSpeed)

        // 0-1, how far vignette extends from center
    // 0-1, edge softness
   // 0-1, circular vs rectangular (legacy mix factor)
// Hero-rewrite params
       // 0=round, 1=oval, 2=square, 3=superellipse
      // 0.3-3.0 — oval/superellipse aspect ratio (X/Y)
     // 0-1 — vignette center X (0.5 = frame center)
     // 0-1 — vignette center Y
      // 0-1 tint R (used when uTintAmount > 0)
      // 0-1 tint G
      // 0-1 tint B
  // 0-1 — 0 = transparent fade (legacy), 1 = solid color fade
   // 0-1 — animated size oscillation amplitude
 // 0-2 — breathing speed (cycles per second / 2π)

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);

  // Animated breathing — size pulses around uSize. Smooth sine wave;
  // uBreathing=0 gives identical behavior to the legacy shader.
  float breath = sin(uTime * uBreathSpeed * 6.28318) * 0.5 + 0.5;
  float effectiveSize = uSize - uBreathing * 0.15 * (breath - 0.5);

  // Centered coordinates with user-movable center.
  vec2 center = vec2(uCenterX, uCenterY);
  vec2 pos = vUv - center;

  // Compute distance based on shape selector.
  int shape = int(uShape + 0.5);
  float dist = 1.0;
  if (shape == 0) {
    // Round (legacy): mix between rectangular and circular via uRoundness.
    float rectDist = max(abs(pos.x), abs(pos.y)) * 2.0;
    float circDist = length(pos) * 2.0;
    dist = mix(rectDist, circDist, uRoundness);
  } else if (shape == 1) {
    // Oval — circular but stretched by uAspect on the X axis.
    float a = max(uAspect, 0.0001);
    dist = length(vec2(pos.x / a, pos.y)) * 2.0;
  } else if (shape == 2) {
    // Square — strict L∞ (max-axis) distance, ignores uAspect/uRoundness.
    dist = max(abs(pos.x), abs(pos.y)) * 2.0;
  } else {
    // Superellipse (squircle) — Lp norm with p=4 gives that rounded-rect
    // look you see in product photography. Stretched by uAspect.
    float a = max(uAspect, 0.0001);
    vec2 q = vec2(pos.x / a, pos.y) * 2.0;
    dist = pow(pow(abs(q.x), 4.0) + pow(abs(q.y), 4.0), 0.25);
  }

  // Smooth falloff across the edge band.
  float vignette = 1.0 - smoothstep(effectiveSize - uSoftness * 0.5, effectiveSize + uSoftness * 0.5, dist);

  // Tint mode: when uTintAmount=0 we fade alpha (legacy behavior).
  // When uTintAmount=1 we KEEP alpha and blend the image toward the
  // tint color in the vignette region — perfect for "stage spotlight"
  // (black surround) or "warm portrait" (orange-brown surround).
  vec3 tint = vec3(uColorR, uColorG, uColorB);
  vec3 finalRgb = mix(texColor.rgb, tint, (1.0 - vignette) * uTintAmount);
  float finalA = texColor.a * mix(vignette, 1.0, uTintAmount);

  gl_FragColor = vec4(finalRgb, finalA);
}
