/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Procedural lens dirt and flare overlay. Adapted from Ghost Arcade's Lens Dirt effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Light & Glow",
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
            "DEFAULT": 0.5
        },
        {
            "NAME": "scaleValue",
            "LABEL": "Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 16.0,
            "DEFAULT": 8.0
        },
        {
            "NAME": "thresholdValue",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "tintWarmth",
            "LABEL": "Tint Warmth",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "scratchesValue",
            "LABEL": "Scratches",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "spotsValue",
            "LABEL": "Spots",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
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
            "NAME": "animSpeed",
            "LABEL": "Anim Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (lensDirtHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uScale (scaleValue)
#define uThreshold (thresholdValue)
#define uTintWarmth (tintWarmth)
#define uScratches (scratchesValue)
#define uSpots (spotsValue)
#define uMode float(floor(modeValue + 0.5))
#define uAnimSpeed (animSpeed)

        // 0-1 dirt overlay strength
         // 1-32 noise pattern scale
     // 0-1 dirt visibility threshold (only show on bright areas)
    // 0-1 warm/cool dust colour
     // 0-1 vertical scratch overlay
         // 0-1 dust spot density
          // 0=screen, 1=add, 2=multiply (debris)

     // 0-1 dirt drift

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  float a = hash21(i), b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uAmount < 0.001) { gl_FragColor = src; return; }

  vec2 driftUv = vUv + vec2(uTime * uAnimSpeed * 0.01, uTime * uAnimSpeed * 0.005);

  // Dust spot field (low-frequency noise)
  float spots = 0.0;
  if (uSpots > 0.001) {
    float n1 = vnoise(driftUv * uScale * 4.0);
    float n2 = vnoise(driftUv * uScale * 8.0 + vec2(13.7, 9.1));
    float n3 = vnoise(driftUv * uScale * 12.0 + vec2(45.2, 71.3));
    spots = (n1 * n2 * n3) * 4.0;
    spots = smoothstep(0.3, 0.6, spots) * uSpots;
  }

  // Vertical scratches
  float scratches = 0.0;
  if (uScratches > 0.001) {
    float xn = hash21(vec2(floor(driftUv.x * uResolution.x * 0.05), 0.0));
    scratches = step(0.97, xn) * uScratches * 0.7;
  }

  float dirt = clamp(spots + scratches, 0.0, 1.0);
  float bright = smoothstep(uThreshold, uThreshold + 0.2, luma(src.rgb));
  dirt *= bright * uAmount;

  vec3 dustColor = mix(vec3(0.9, 0.95, 1.0), vec3(1.0, 0.85, 0.65), uTintWarmth);
  vec3 dirtRgb = dustColor * dirt;

  int mode = int(uMode + 0.5);
  vec3 result;
  if (mode == 0) {
    result = 1.0 - (1.0 - src.rgb) * (1.0 - dirtRgb);
  } else if (mode == 1) {
    result = src.rgb + dirtRgb;
  } else {
    result = src.rgb * (1.0 - dirt * 0.5);
  }
  gl_FragColor = vec4(result, src.a);
}
