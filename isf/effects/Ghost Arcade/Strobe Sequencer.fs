/*{
    "ISFVSN": "2",
    "DESCRIPTION": "BPM-synced strobe with editable 16-step pattern (bitmask). On/Off / Invert / Tint / Zoom modes + swing. Adapted from Ghost Arcade's Strobe Sequencer effect.",
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
            "NAME": "bPM",
            "LABEL": "BPM",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 240.0,
            "DEFAULT": 120.0
        },
        {
            "NAME": "stepsValue",
            "LABEL": "Steps",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 32.0,
            "DEFAULT": 16.0
        },
        {
            "NAME": "patternValue",
            "LABEL": "Pattern",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 43690.0,
            "DEFAULT": 21845.0
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
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
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
            "NAME": "swingValue",
            "LABEL": "Swing",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (strobeSequencerHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uBPM (bPM)
#define uSteps (stepsValue)
#define uPattern (patternValue)
#define uMode float(floor(modeValue + 0.5))
#define uIntensity (intensity)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uSwing (swingValue)

           // 30-240
         // 4-16 step count per bar
       // bit-encoded pattern (0..(2^16-1))
          // 0=on/off, 1=invert, 2=tint, 3=zoom
     // 0-2



         // 0-0.5 (offbeat shift)

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  int steps = int(clamp(uSteps, 1.0, 16.0));
  float beatLen = 60.0 / max(1.0, uBPM);
  float stepLen = beatLen / float(steps) * 4.0; // 16 steps = 1 bar @4/4
  float t = uTime;
  float stepF = mod(t / stepLen, float(steps));
  int stepIdx = int(stepF);
  // Apply swing on odd steps
  float frac = fract(stepF);
  if (stepIdx % 2 == 1) frac = clamp(frac - uSwing, 0.0, 1.0);

  // Decode pattern bit (uPattern is treated as bitmask)
  float patternF = uPattern;
  float bitVal = mod(floor(patternF / pow(2.0, float(stepIdx))), 2.0);
  float gate = (bitVal > 0.5 && frac < 0.5) ? 1.0 : 0.0;

  int mode = int(uMode + 0.5);
  vec3 tint = vec3(uTintR, uTintG, uTintB);
  vec3 result = src;
  if (mode == 0) {
    result = mix(src, src + tint * uIntensity, gate);
  } else if (mode == 1) {
    result = mix(src, 1.0 - src, gate * uIntensity);
  } else if (mode == 2) {
    result = mix(src, src * tint + tint * 0.4, gate * uIntensity);
  } else {
    // Zoom-on-beat
    vec2 d = vUv - 0.5;
    float zoom = 1.0 + gate * uIntensity * 0.1;
    vec2 sUv = 0.5 + d / zoom;
    sUv = clamp(sUv, vec2(0.0), vec2(1.0));
    result = IMG_NORM_PIXEL(inputImage, sUv).rgb;
  }
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
