/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Sine wave distortion — horizontal, vertical, radial. Adapted from Ghost Arcade's Wave effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Distort",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "waveAmplitude",
            "LABEL": "Amplitude",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 0.1,
            "DEFAULT": 0.02
        },
        {
            "NAME": "waveFrequency",
            "LABEL": "Frequency",
            "TYPE": "float",
            "MIN": 1.0,
            "MAX": 20.0,
            "DEFAULT": 5.0
        },
        {
            "NAME": "waveSpeed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 5.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "typeValue",
            "LABEL": "Type",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "waveformValue",
            "LABEL": "Waveform",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "phaseValue",
            "LABEL": "Phase",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "secondaryAmt",
            "LABEL": "Secondary Amt",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "chromaSplit",
            "LABEL": "Chroma Split",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (waveHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmplitude (waveAmplitude)
#define uFrequency (waveFrequency)
#define uSpeed (waveSpeed)
#define uType float(floor(typeValue + 0.5))
#define uWaveform (waveformValue)
#define uPhase (phaseValue)
#define uSecondaryAmt (secondaryAmt)
#define uChromaSplit (chromaSplit)

     // 0-50 px
     // 0.1-30
         // 0-3
          // 0=horizontal, 1=vertical, 2=radial, 3=swirl
      // 0=sin, 1=triangle, 2=saw, 3=square
         // 0-360 phase offset
  // 0-1 second harmonic
   // 0-1 RGB phase shift

float waveform(float x, int kind) {
  if (kind == 0) return sin(x);
  if (kind == 1) return abs(mod(x / 3.14159, 2.0) - 1.0) * 2.0 - 1.0;
  if (kind == 2) return mod(x / 3.14159, 2.0) - 1.0;
  return sign(sin(x));
}

vec2 waveOffset(vec2 uv, float phaseShift) {
  int type = int(uType + 0.5);
  int wf = int(uWaveform + 0.5);
  float t = uTime * uSpeed + radians(uPhase) + phaseShift;
  float amp = uAmplitude / uResolution.y;
  float freq = uFrequency;
  vec2 off = vec2(0.0);

  if (type == 0) {
    // Horizontal — wave shifts X based on Y
    off.x = waveform(uv.y * freq + t, wf) * amp;
    if (uSecondaryAmt > 0.001) off.x += waveform(uv.y * freq * 2.5 + t * 1.7, wf) * amp * uSecondaryAmt * 0.5;
  } else if (type == 1) {
    off.y = waveform(uv.x * freq + t, wf) * amp;
    if (uSecondaryAmt > 0.001) off.y += waveform(uv.x * freq * 2.5 + t * 1.7, wf) * amp * uSecondaryAmt * 0.5;
  } else if (type == 2) {
    // Radial — push out/in based on distance
    vec2 d = uv - 0.5;
    float r = length(d);
    vec2 dir = (r > 0.001) ? d / r : vec2(1.0, 0.0);
    off = dir * waveform(r * freq * 8.0 + t, wf) * amp;
  } else {
    // Swirl
    vec2 d = uv - 0.5;
    float r = length(d);
    float a = atan(d.y, d.x);
    float w = waveform(r * freq + t, wf) * amp * 4.0;
    a += w;
    off = vec2(cos(a), sin(a)) * r + 0.5 - uv;
  }
  return off;
}

void main() {
  vec2 baseOff = waveOffset(vUv, 0.0);
  vec3 col;
  if (uChromaSplit > 0.001) {
    vec2 offR = waveOffset(vUv, 0.5 * uChromaSplit);
    vec2 offB = waveOffset(vUv, -0.5 * uChromaSplit);
    col.r = IMG_NORM_PIXEL(inputImage, vUv + offR).r;
    col.g = IMG_NORM_PIXEL(inputImage, vUv + baseOff).g;
    col.b = IMG_NORM_PIXEL(inputImage, vUv + offB).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, vUv + baseOff).rgb;
  }
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(col, src.a);
}
