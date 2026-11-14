/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Expanding ring shockwave distortion. Adapted from Ghost Arcade's Shockwave effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Warp",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "triggerTime",
            "LABEL": "Trigger Time",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "speedValue",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "amplitude",
            "LABEL": "Amplitude",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.06
        },
        {
            "NAME": "ringWidth",
            "LABEL": "Ring Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.15
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
            "NAME": "chromatic",
            "LABEL": "Chromatic",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (shockwaveHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uTriggerTime (triggerTime)
#define uSpeed (speedValue)
#define uAmplitude (amplitude)
#define uRingWidth (ringWidth)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uChromatic (chromatic)
#define uMode float(floor(modeValue + 0.5))

   // time when wave was triggered
         // 0.1-3 expansion speed
     // 0-0.2 distortion strength
     // 0.01-0.5


     // 0-1
          // 0=looping continuous, 1=one-shot

vec2 shockOffset(vec2 uv, float waveTime) {
  vec2 c = vec2(uCenterX, uCenterY);
  vec2 d = uv - c;
  d.x *= uResolution.x / uResolution.y;
  float r = length(d);
  float ringR = waveTime * uSpeed;
  float band = smoothstep(uRingWidth * 0.5, 0.0, abs(r - ringR));
  vec2 dir = (r > 0.001) ? d / r : vec2(1.0, 0.0);
  dir.x *= uResolution.y / uResolution.x;
  return dir * band * uAmplitude;
}

void main() {
  int mode = int(uMode + 0.5);
  float waveTime;
  if (mode == 0) {
    // Looping
    waveTime = mod(uTime, 2.0 / max(0.1, uSpeed));
  } else {
    // One-shot
    waveTime = max(0.0, uTime - uTriggerTime);
  }

  vec2 baseOff = shockOffset(vUv, waveTime);
  vec3 col;
  if (uChromatic > 0.001) {
    vec2 offR = shockOffset(vUv, waveTime + 0.05 * uChromatic);
    vec2 offB = shockOffset(vUv, waveTime - 0.05 * uChromatic);
    col.r = IMG_NORM_PIXEL(inputImage, vUv + offR).r;
    col.g = IMG_NORM_PIXEL(inputImage, vUv + baseOff).g;
    col.b = IMG_NORM_PIXEL(inputImage, vUv + offB).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, vUv + baseOff).rgb;
  }
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(col, src.a);
}
