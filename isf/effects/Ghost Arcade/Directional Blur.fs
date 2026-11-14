/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Motion blur along a specified angle. Adapted from Ghost Arcade's Directional Blur effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Blur & Focus",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "amount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.25
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
            "NAME": "falloffValue",
            "LABEL": "Falloff",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "centerBias",
            "LABEL": "Center Bias",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
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

// Ported from Ghost Arcade (directionalBlurHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uAngle (angle)
#define uSamples (samplesValue)
#define uFalloff (falloffValue)
#define uCenterBias (centerBias)
#define uMix (mixValue)

        // 0-1 normalized blur length
         // 0-360 degrees
       // 4-32
       // 0-1 weight falloff toward edges
    // 0-1 keep center sharper
           // 0-1 wet/dry

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uAmount < 0.001) { gl_FragColor = src; return; }

  int samples = int(clamp(uSamples, 2.0, 32.0));
  float ang = radians(uAngle);
  vec2 dir = vec2(cos(ang), sin(ang));
  vec2 texel = 1.0 / uResolution;
  float maxOffset = uAmount * 0.3; // 30% of screen max

  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  for (int i = -32; i <= 32; i++) {
    if (abs(i) > samples) continue;
    float t = float(i) / float(samples);
    vec2 off = dir * t * maxOffset;
    // Falloff weight (1 at center, drops toward edges)
    float w = mix(1.0, 1.0 - abs(t), uFalloff);
    // Center bias: weight center heavier
    w *= mix(1.0, exp(-t * t * 8.0), uCenterBias);
    acc += IMG_NORM_PIXEL(inputImage, vUv + off).rgb * w;
    wsum += w;
  }
  vec3 blurred = acc / wsum;
  gl_FragColor = vec4(mix(src.rgb, blurred, uMix), src.a);
}
