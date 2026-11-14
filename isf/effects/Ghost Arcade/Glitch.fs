/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Digital glitch with RGB split and block artifacts. Adapted from Ghost Arcade's Glitch effect.",
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
            "NAME": "glitchIntensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "glitchSpeed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 5.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "blockSize",
            "LABEL": "Block Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "glitchRGBSplit",
            "LABEL": "RGB Split",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.02
        },
        {
            "NAME": "jitter",
            "LABEL": "Jitter",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "triggerMode",
            "LABEL": "Trigger Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "blockHold",
            "LABEL": "Block Hold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "verticalSlice",
            "LABEL": "Vertical Slice",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "freezeBurst",
            "LABEL": "Freeze Burst",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "tearChance",
            "LABEL": "Tear Chance",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (glitchShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (glitchIntensity)
#define uSpeed (glitchSpeed)
#define uBlockSize (blockSize)
#define uRGBSplit (glitchRGBSplit)
#define uJitter (jitter)
#define uTriggerMode float(floor(triggerMode + 0.5))
#define uBlockHold (blockHold)
#define uVerticalSlice (verticalSlice)
#define uFreezeBurst (freezeBurst)
#define uTearChance (tearChance)
#define uAudio 0.0

float random(float seed) { return fract(sin(seed * 12.9898) * 43758.5453); }
float random2(vec2 st) { return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
  vec2 uv = vUv;
  float t = uTime * max(uSpeed, 0.01);

  float glitchTrigger = 0.0;
  int mode = int(uTriggerMode + 0.5);
  if (mode == 1) {
    glitchTrigger = clamp(uAudio * 1.6, 0.0, 1.0) * uIntensity;
  } else if (mode == 2) {
    float beatPhase = floor(uTime * 8.0);
    float beatHit = step(0.4, uAudio) * step(0.7, random(beatPhase));
    glitchTrigger = beatHit * uIntensity;
  } else {
    glitchTrigger = step(0.95, random(floor(t * 10.0))) * uIntensity;
  }

  float blockHeight = max(uBlockSize * 0.1, 0.01);
  float block = floor(uv.y / blockHeight);
  float blockTime = floor(t * mix(20.0, 1.0, uBlockHold));
  float blockRandom = random(block + blockTime);
  if (blockRandom > 1.0 - uIntensity * 0.3 && glitchTrigger > 0.0) {
    uv.x += (random(block + t) - 0.5) * uIntensity * 0.2;
  }

  float colSeed = floor(uv.x * uResolution.x / 12.0);
  float colHit = step(1.0 - uVerticalSlice * 0.2, random(colSeed + floor(t * 7.0)));
  if (colHit > 0.5 && glitchTrigger > 0.0) {
    uv.y += (random(colSeed + 13.7) - 0.5) * uVerticalSlice * 0.18;
    uv.y = fract(uv.y);
  }

  float lineJitter = (random2(vec2(floor(uv.y * uResolution.y), floor(t * 20.0))) - 0.5);
  uv.x += lineJitter * uJitter * 0.01 * glitchTrigger;

  float rgbAmount = uRGBSplit * 0.02 * (1.0 + glitchTrigger * 3.0);
  float tearBand = step(1.0 - uTearChance * 0.3, random(floor(uv.y * 50.0) + floor(t * 6.0)));
  if (tearBand > 0.5 && glitchTrigger > 0.0) rgbAmount *= 6.0;
  vec4 color;
  color.r = IMG_NORM_PIXEL(inputImage, vec2(uv.x + rgbAmount, uv.y)).r;
  color.g = IMG_NORM_PIXEL(inputImage, uv).g;
  color.b = IMG_NORM_PIXEL(inputImage, vec2(uv.x - rgbAmount, uv.y)).b;
  color.a = IMG_NORM_PIXEL(inputImage, uv).a;

  float freezeHit = step(1.0 - uFreezeBurst * 0.25, random(floor(t * 3.0))) * glitchTrigger;
  if (freezeHit > 0.5) {
    vec2 punchUv = vec2(fract(vUv.x + 0.37 + 0.13 * random(block)),
                        fract(vUv.y + 0.21 + 0.17 * random(block + 4.0)));
    vec4 punch = IMG_NORM_PIXEL(inputImage, punchUv);
    color.rgb = mix(color.rgb, punch.rgb, 0.7);
  }

  if (random(floor(t * 15.0) + block) > 0.98 && glitchTrigger > 0.0) {
    color.rgb = 1.0 - color.rgb;
  }

  gl_FragColor = color;
}
