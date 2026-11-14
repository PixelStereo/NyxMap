/*{
    "ISFVSN": "2",
    "DESCRIPTION": "VHS tape dropout — horizontal band loss with noise. Adapted from Ghost Arcade's Tape Dropout effect.",
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
            "NAME": "density",
            "LABEL": "Density",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "lengthValue",
            "LABEL": "Length",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "color",
            "LABEL": "Color",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
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
            "NAME": "noiseAmp",
            "LABEL": "Noise Amp",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "mixValue",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (tapeDropoutHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uDensity (density)
#define uLength (lengthValue)
#define uColor (color)
#define uSpeed (speed)
#define uNoiseAmp (noiseAmp)
#define uMix (mixValue)

       // 0-1
        // 0-1 stripe length
         // 0=white, 1=mono, 2=glitch hue
         // 0-3
      // 0-1
           // 0-1

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uDensity < 0.001) { gl_FragColor = src; return; }

  float t = floor(uTime * uSpeed * 8.0) / 8.0;
  float yBucket = floor(vUv.y * 80.0);
  float trigger = hash21(vec2(yBucket, t));
  float lenH = hash21(vec2(yBucket + 13.0, t));
  float startX = hash21(vec2(yBucket + 27.0, t));
  float length = uLength * mix(0.05, 0.5, lenH);

  float inStripe = step(1.0 - uDensity * 0.4, trigger)
                 * step(startX, vUv.x)
                 * step(vUv.x, startX + length);

  if (inStripe < 0.5) { gl_FragColor = src; return; }

  int colorMode = int(uColor + 0.5);
  float n = hash21(vec2(vUv.x * uResolution.x, t * 100.0));
  vec3 stripe;
  if (colorMode == 0) stripe = vec3(n);
  else if (colorMode == 1) stripe = vec3(n * 0.6 + 0.2);
  else {
    float hue = hash21(vec2(yBucket, t * 13.0));
    stripe = mix(vec3(1, 0, 0.4), vec3(0, 1, 0.6), hue);
    stripe = mix(stripe, vec3(0.4, 0.4, 1), n);
  }
  stripe = mix(stripe, src.rgb, 1.0 - uNoiseAmp);

  gl_FragColor = vec4(mix(src.rgb, stripe, uMix * inStripe), src.a);
}
