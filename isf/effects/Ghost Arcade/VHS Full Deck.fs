/*{
    "ISFVSN": "2",
    "DESCRIPTION": "All-in-one VHS: tracking + head switch + chroma bleed + dropouts + tape noise + scanlines + saturation. Adapted from Ghost Arcade's VHS Full Deck effect.",
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
            "NAME": "trackingValue",
            "LABEL": "Tracking",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "headSwitch",
            "LABEL": "Head Switch",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "chromaBleed",
            "LABEL": "Chroma Bleed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "dropouts",
            "LABEL": "Dropouts",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "tapeNoise",
            "LABEL": "Tape Noise",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "scanlines",
            "LABEL": "Scanlines",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "colorBleed",
            "LABEL": "Color Bleed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "saturation",
            "LABEL": "Saturation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
        },
        {
            "NAME": "trackingJump",
            "LABEL": "Tracking Jump",
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
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (vhsFullDeckHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uTracking (trackingValue)
#define uHeadSwitch (headSwitch)
#define uChromaBleed (chromaBleed)
#define uDropouts (dropouts)
#define uTapeNoise (tapeNoise)
#define uScanlines (scanlines)
#define uColorBleed (colorBleed)
#define uSaturation (saturation)
#define uTrackingJump (trackingJump)
#define uMode float(floor(modeValue + 0.5))

      // 0-1 vertical sync drift
    // 0-1 head switching noise band
   // 0-1
      // 0-1
     // 0-1
     // 0-1
    // 0-1
    // 0-1.5
  // 0-1
          // 0=clean, 1=worn, 2=destroyed

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec2 uv = vUv;
  int mode = int(uMode + 0.5);
  float modeBoost = (mode == 1) ? 1.3 : (mode == 2) ? 1.8 : 1.0;

  // Vertical tracking drift
  float trackJit = sin(uv.y * 30.0 + uTime * 2.0) * 0.005 * uTracking * modeBoost;
  uv.x += trackJit;

  // Tracking jump: occasional offset
  if (uTrackingJump > 0.001) {
    float jump = step(0.97, hash21(vec2(floor(uTime * 4.0), 0.0))) * uTrackingJump;
    if (uv.y < 0.4) uv.x += jump * 0.05;
  }

  // Head switch noise band at bottom
  float headBand = smoothstep(0.05, 0.0, uv.y) * uHeadSwitch * modeBoost;
  if (headBand > 0.01) {
    uv.x += (hash21(vec2(uv.y * 100.0, floor(uTime * 8.0))) - 0.5) * 0.04;
  }

  // Sample with chroma bleed
  vec3 col;
  float cb = uChromaBleed * modeBoost * 0.04;
  col.r = IMG_NORM_PIXEL(inputImage, uv + vec2(cb, 0.0)).r;
  col.g = IMG_NORM_PIXEL(inputImage, uv).g;
  col.b = IMG_NORM_PIXEL(inputImage, uv - vec2(cb, 0.0)).b;

  // Color bleed (horizontal smear)
  if (uColorBleed > 0.001) {
    float bleed = uColorBleed * modeBoost;
    col.r = mix(col.r, IMG_NORM_PIXEL(inputImage, uv + vec2(0.02 * bleed, 0)).r, 0.4);
  }

  // Saturation
  float l = luma(col);
  col = mix(vec3(l), col, uSaturation);

  // Tape noise (added grain)
  if (uTapeNoise > 0.001) {
    float n = (hash21(vUv * uResolution + uTime * 60.0) - 0.5) * uTapeNoise * modeBoost * 0.4;
    col += vec3(n);
  }

  // Scanlines
  if (uScanlines > 0.001) {
    float sl = sin(uv.y * 800.0) * 0.5 + 0.5;
    col *= mix(1.0, sl * 0.6 + 0.4, uScanlines * modeBoost);
  }

  // Dropouts: random horizontal bright stripes
  if (uDropouts > 0.001) {
    float yB = floor(uv.y * 60.0);
    float drop = step(0.93, hash21(vec2(yB, floor(uTime * 6.0))));
    col += vec3(drop * uDropouts * modeBoost * 0.6);
  }

  // Head band overlay (washes color)
  col += vec3(headBand * 0.5);

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
