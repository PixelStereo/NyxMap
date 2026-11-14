/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Circular/spin blur around center. Adapted from Ghost Arcade's Radial Blur effect.",
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
            "DEFAULT": 0.3
        },
        {
            "NAME": "radiusInner",
            "LABEL": "Radius Inner",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "radiusOuter",
            "LABEL": "Radius Outer",
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

// Ported from Ghost Arcade (radialBlurHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uSamples (samplesValue)
#define uFalloff (falloff)
#define uRadiusInner (radiusInner)
#define uRadiusOuter (radiusOuter)
#define uMix (mixValue)

        // 0-1 spin angle (radians scale)
       // 0-1
       // 0-1
       // 4-32
       // 0-1
   // 0-1 unblurred inner radius
   // 0-1 fully blurred outer radius
           // 0-1 wet/dry

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uAmount < 0.001) { gl_FragColor = src; return; }

  int samples = int(clamp(uSamples, 2.0, 32.0));
  vec2 center = vec2(uCenterX, uCenterY);
  vec2 d = vUv - center;
  float dist = length(d);

  // Mask: 0 inside inner radius, 1 outside outer radius
  float mask = smoothstep(uRadiusInner, uRadiusOuter, dist);
  if (mask < 0.001) { gl_FragColor = src; return; }

  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  float maxAngle = uAmount * 1.2; // ~70° max spin
  for (int i = -32; i <= 32; i++) {
    if (abs(i) > samples) continue;
    float t = float(i) / float(samples);
    float a = t * maxAngle * mask;
    float ca = cos(a), sa = sin(a);
    vec2 rd = vec2(d.x * ca - d.y * sa, d.x * sa + d.y * ca);
    vec2 sUv = center + rd;
    float w = mix(1.0, 1.0 - abs(t), uFalloff);
    acc += IMG_NORM_PIXEL(inputImage, sUv).rgb * w;
    wsum += w;
  }
  vec3 blurred = acc / wsum;
  gl_FragColor = vec4(mix(src.rgb, blurred, uMix * mask), src.a);
}
