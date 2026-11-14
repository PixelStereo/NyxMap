/*{
    "ISFVSN": "2",
    "DESCRIPTION": "VHS tape artifacts — tracking, color bleed, scanlines. Adapted from Ghost Arcade's VHS effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Stylize",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "vhsTracking",
            "LABEL": "Tracking",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "vhsNoise",
            "LABEL": "Noise",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "vhsDistortion",
            "LABEL": "Distortion",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "vhsColorBleed",
            "LABEL": "Color Bleed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "scanlines",
            "LABEL": "Scanlines",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "headSwitch",
            "LABEL": "Head Switch",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "tapeWobble",
            "LABEL": "Tape Wobble",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "dropout",
            "LABEL": "Dropout",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "chromaDelay",
            "LABEL": "Chroma Delay",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "trackingJump",
            "LABEL": "Tracking Jump",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "saturation",
            "LABEL": "Saturation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (vhsShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uTracking (vhsTracking)
#define uNoise (vhsNoise)
#define uDistortion (vhsDistortion)
#define uColorBleed (vhsColorBleed)
#define uScanlines (scanlines)
#define uHeadSwitch (headSwitch)
#define uTapeWobble (tapeWobble)
#define uDropout (dropout)
#define uChromaDelay (chromaDelay)
#define uTrackingJump (trackingJump)
#define uSaturation (saturation)

float hash11(float seed) { return fract(sin(seed * 12.9898) * 43758.5453123); }
float random(vec2 st) { return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123); }
float noise(vec2 st) {
  vec2 i = floor(st);
  vec2 f = fract(st);
  float a = random(i);
  float b = random(i + vec2(1.0, 0.0));
  float c = random(i + vec2(0.0, 1.0));
  float d = random(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

void main() {
  vec2 uv = vUv;

  // Tracking jump (vertical roll w/ snap).
  float jumpTrigger = step(0.985, hash11(floor(uTime * 1.3))) * uTrackingJump;
  float jumpAmount = uTrackingJump * 0.08 * (sin(uTime * 0.7) * 0.5 + 0.5);
  uv.y = fract(uv.y + jumpAmount + jumpTrigger * 0.4);

  // Tape wobble (capstan jitter).
  float wobble = sin(uv.y * 4.0 + uTime * 1.5) * 0.6
               + sin(uv.y * 11.0 + uTime * 0.7) * 0.4;
  uv.x += wobble * uTapeWobble * 0.012;

  // Tracking distortion (legacy).
  float trackingOffset = sin(uv.y * 10.0 + uTime * 3.0) * uTracking * 0.02;
  trackingOffset += step(0.99, random(vec2(uTime * 0.1, uv.y))) * uTracking * 0.1;
  uv.x += trackingOffset;

  // Wave distortion (legacy).
  uv.x += sin(uv.y * 50.0 + uTime * 10.0) * uDistortion * 0.003;
  uv.y += sin(uv.x * 30.0 + uTime * 8.0) * uDistortion * 0.002;

  // Head-switch tear band at bottom of frame.
  float headBand = smoothstep(0.06, 0.0, uv.y);
  float headTear = (random(vec2(floor(uv.y * 200.0), floor(uTime * 30.0))) - 0.5)
                 * headBand * uHeadSwitch * 0.06;
  uv.x += headTear;

  // Sample with chroma bleed + delay.
  float bleedAmount = uColorBleed * 0.005;
  float chromaLag = uChromaDelay * 0.012;
  vec4 color;
  color.r = IMG_NORM_PIXEL(inputImage, vec2(uv.x + bleedAmount + chromaLag, uv.y)).r;
  color.g = IMG_NORM_PIXEL(inputImage, uv).g;
  color.b = IMG_NORM_PIXEL(inputImage, vec2(uv.x - bleedAmount - chromaLag, uv.y)).b;
  color.a = IMG_NORM_PIXEL(inputImage, uv).a;

  // Dropout bands.
  float dropoutSeed = floor(uv.y * uResolution.y * 0.5) + floor(uTime * 4.0);
  float dropoutHit = step(1.0 - uDropout * 0.04, hash11(dropoutSeed));
  if (dropoutHit > 0.5) {
    float dropoutKind = hash11(dropoutSeed + 7.3);
    if (dropoutKind > 0.5) color.rgb = mix(color.rgb, vec3(1.0), 0.85);
    else                    color.rgb = mix(color.rgb, vec3(0.0), 0.85);
  }

  // Luma noise.
  float n = noise(uv * uResolution * 0.5 + uTime * 100.0);
  color.rgb += (n - 0.5) * uNoise * 0.3;

  // Scanlines.
  float scanline = sin(vUv.y * uResolution.y * 2.0) * 0.5 + 0.5;
  color.rgb *= 1.0 - uScanlines * 0.3 * scanline;

  // Saturation pull-back.
  vec3 luminance = vec3(0.299, 0.587, 0.114);
  float lum = dot(color.rgb, luminance);
  float satMix = clamp(1.0 - uSaturation, 0.0, 1.0);
  color.rgb = mix(color.rgb, vec3(lum), satMix * 0.6 + uTracking * 0.2);

  gl_FragColor = vec4(clamp(color.rgb, 0.0, 1.0), color.a);
}
