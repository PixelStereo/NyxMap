/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Long-exposure light trail streaks from bright pixels. Adapted from Ghost Arcade's Motion Trails effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Trails",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "lengthValue",
            "LABEL": "Length",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "samplesValue",
            "LABEL": "Samples",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 32.0,
            "DEFAULT": 16.0
        },
        {
            "NAME": "falloff",
            "LABEL": "Falloff",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "chromaSplit",
            "LABEL": "Chroma Split",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "mode",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (motionTrailsHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uLength (lengthValue)
#define uAngle (angle)
#define uSamples (samplesValue)
#define uFalloff (falloff)
#define uChromaSplit (chromaSplit)
#define uMode float(floor(mode + 0.5))

        // 0-1
         // 0-360
       // 4-32
       // 0-1
   // 0-1
          // 0=fade, 1=copy

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uLength < 0.001) { gl_FragColor = src; return; }

  int samples = int(clamp(uSamples, 4.0, 32.0));
  float ang = radians(uAngle);
  vec2 dir = vec2(cos(ang), sin(ang));
  vec2 step = dir * uLength * 0.3;

  vec3 acc = src.rgb;
  float wsum = 1.0;
  for (int i = 1; i <= 32; i++) {
    if (i > samples) break;
    float t = float(i) / float(samples);
    vec2 sUv = vUv + step * t;
    vec3 sCol;
    if (uChromaSplit > 0.001) {
      sCol.r = IMG_NORM_PIXEL(inputImage, sUv + dir * t * uChromaSplit * 0.02).r;
      sCol.g = IMG_NORM_PIXEL(inputImage, sUv).g;
      sCol.b = IMG_NORM_PIXEL(inputImage, sUv - dir * t * uChromaSplit * 0.02).b;
    } else {
      sCol = IMG_NORM_PIXEL(inputImage, sUv).rgb;
    }
    float w = (uMode > 0.5) ? 1.0 : pow(1.0 - t, max(0.5, uFalloff * 4.0));
    acc += sCol * w;
    wsum += w;
  }
  gl_FragColor = vec4(acc / wsum, src.a);
}
