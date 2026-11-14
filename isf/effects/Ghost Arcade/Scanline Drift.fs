/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Animated scanline bands that drift and offset. Adapted from Ghost Arcade's Scanline Drift effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Stylize",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "frequencyValue",
            "LABEL": "Frequency",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 160.0,
            "DEFAULT": 80.0
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "waveform",
            "LABEL": "Waveform",
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
            "DEFAULT": 0.3
        },
        {
            "NAME": "chunkiness",
            "LABEL": "Chunkiness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (scanlineDriftHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (intensity)
#define uFrequency (frequencyValue)
#define uSpeed (speed)
#define uWaveform (waveform)
#define uChromaSplit (chromaSplit)
#define uChunkiness (chunkiness)

     // 0-1
     // 1-200 line frequency
         // 0-3
      // 0=sin, 1=noise, 2=sawtooth
   // 0-1
    // 0-1 hold for N rows

float hash11(float n) { return fract(sin(n) * 43758.5453); }

void main() {
  if (uIntensity < 0.001) { gl_FragColor = IMG_NORM_PIXEL(inputImage, vUv); return; }

  int wf = int(uWaveform + 0.5);
  float yLine = vUv.y;
  if (uChunkiness > 0.001) {
    float chunk = mix(1.0, 32.0, uChunkiness);
    yLine = floor(vUv.y * uResolution.y / chunk) * chunk / uResolution.y;
  }
  float t = uTime * uSpeed;
  float drift;
  if (wf == 0) drift = sin(yLine * uFrequency + t);
  else if (wf == 1) drift = (hash11(floor(yLine * uFrequency) + floor(t * 8.0)) - 0.5) * 2.0;
  else drift = mod(yLine * uFrequency + t, 1.0) * 2.0 - 1.0;
  drift *= uIntensity * 0.05;

  vec3 col;
  if (uChromaSplit > 0.001) {
    float r = IMG_NORM_PIXEL(inputImage, vec2(vUv.x + drift * (1.0 + uChromaSplit * 0.3), vUv.y)).r;
    float g = IMG_NORM_PIXEL(inputImage, vec2(vUv.x + drift, vUv.y)).g;
    float b = IMG_NORM_PIXEL(inputImage, vec2(vUv.x + drift * (1.0 - uChromaSplit * 0.3), vUv.y)).b;
    col = vec3(r, g, b);
  } else {
    col = IMG_NORM_PIXEL(inputImage, vec2(vUv.x + drift, vUv.y)).rgb;
  }
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(col, src.a);
}
