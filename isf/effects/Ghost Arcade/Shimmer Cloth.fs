/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Image becomes waving fabric with thread weave (horizontal / plaid / satin) + silk shimmer highlights. Adapted from Ghost Arcade's Shimmer Cloth effect.",
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
            "NAME": "amplitude",
            "LABEL": "Amplitude",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "frequency",
            "LABEL": "Frequency",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 16.0,
            "DEFAULT": 8.0
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "threadDensity",
            "LABEL": "Thread Density",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 120.0,
            "DEFAULT": 60.0
        },
        {
            "NAME": "threadDepth",
            "LABEL": "Thread Depth",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "shimmerValue",
            "LABEL": "Shimmer",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
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

// Ported from Ghost Arcade (shimmerClothHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmplitude (amplitude)
#define uFrequency (frequency)
#define uSpeed (speed)
#define uThreadDensity (threadDensity)
#define uThreadDepth (threadDepth)
#define uShimmer (shimmerValue)
#define uMode float(floor(modeValue + 0.5))

     // 0-1
     // 1-30
         // 0-3
 // 1-200 thread weave count
   // 0-1 thread shadow depth
       // 0-2 silk shimmer intensity
          // 0=horizontal weave, 1=plaid, 2=satin

void main() {
  // Wave the UV
  float t = uTime * uSpeed;
  vec2 wave;
  wave.x = sin(vUv.y * uFrequency + t) * uAmplitude * 0.04;
  wave.y = cos(vUv.x * uFrequency * 0.8 + t * 0.7) * uAmplitude * 0.03;
  vec2 sUv = vUv + wave;
  sUv = clamp(sUv, vec2(0.0), vec2(1.0));
  vec3 col = IMG_NORM_PIXEL(inputImage, sUv).rgb;

  // Thread weave overlay
  int mode = int(uMode + 0.5);
  vec2 px = sUv * uResolution;
  float thread;
  if (mode == 0) {
    // Horizontal weave
    thread = sin(px.y * uThreadDensity * 0.1) * 0.5 + 0.5;
  } else if (mode == 1) {
    // Plaid (both axes)
    thread = (sin(px.x * uThreadDensity * 0.07) + sin(px.y * uThreadDensity * 0.07)) * 0.25 + 0.5;
  } else {
    // Satin (45-degree weave)
    thread = sin((px.x + px.y) * uThreadDensity * 0.06) * 0.5 + 0.5;
  }
  col *= mix(1.0, thread, uThreadDepth * 0.4);

  // Shimmer (silk highlights)
  if (uShimmer > 0.001) {
    float specular = pow(max(0.0, sin(px.x * 0.1 + px.y * 0.05 + t * 1.5)), 8.0);
    col += vec3(specular) * uShimmer * 0.3;
  }
  gl_FragColor = vec4(col, 1.0);
}
