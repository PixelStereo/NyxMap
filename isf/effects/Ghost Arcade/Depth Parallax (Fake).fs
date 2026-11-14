/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Estimate depth from luma → render multiple parallax layers. Push / pan / swing modes with chromatic depth. Adapted from Ghost Arcade's Depth Parallax (Fake) effect.",
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
            "NAME": "depthStrength",
            "LABEL": "Depth Strength",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "pushIn",
            "LABEL": "Push In",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "layersValue",
            "LABEL": "Layers",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
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
            "NAME": "depthBoost",
            "LABEL": "Depth Boost",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
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
            "NAME": "panX",
            "LABEL": "Pan X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "panY",
            "LABEL": "Pan Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (depthParallaxHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uDepthStrength (depthStrength)
#define uPushIn (pushIn)
#define uLayers (layersValue)
#define uChromatic (chromatic)
#define uDepthBoost (depthBoost)
#define uMode float(floor(modeValue + 0.5))
#define uPanX (panX)
#define uPanY (panY)

 // 0-1
        // 0-1 base zoom into depth
        // 1-8 depth layer count
     // 0-1 RGB depth split
    // 0-2 luma → depth response
          // 0=push, 1=pan, 2=swing
          // -1..1
          // -1..1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec2 parallaxMap(vec2 uv, float depth, float layer) {
  int mode = int(uMode + 0.5);
  vec2 d = uv - 0.5;
  float scale = 1.0 + (depth - 0.5) * uDepthStrength * 0.5 + uPushIn * 0.3 * (1.0 + layer * 0.1);
  vec2 sUv;
  if (mode == 0) {
    sUv = 0.5 + d / scale;
  } else if (mode == 1) {
    sUv = uv + vec2(uPanX, uPanY) * (depth - 0.5) * uDepthStrength * 0.2 * (1.0 + layer * 0.2);
  } else {
    float sw = sin(uTime * 0.5 + layer * 0.3) * 0.5;
    sUv = uv + vec2(sw, sw * 0.4) * (depth - 0.5) * uDepthStrength * 0.15;
  }
  return clamp(sUv, vec2(0.0), vec2(1.0));
}

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  float baseDepth = pow(luma(src), max(0.1, uDepthBoost));
  int layers = int(clamp(uLayers, 1.0, 8.0));

  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  for (int i = 0; i < 8; i++) {
    if (i >= layers) break;
    float layer = float(i) / max(1.0, float(layers - 1));
    float sliceDepth = mix(0.2, 0.95, layer);
    // Estimate per-layer offset
    vec2 sUv = parallaxMap(vUv, sliceDepth, layer);
    vec3 sCol;
    if (uChromatic > 0.001) {
      vec2 cd = (sUv - 0.5) * uChromatic * 0.04 * (1.0 - layer);
      sCol.r = IMG_NORM_PIXEL(inputImage, sUv + cd).r;
      sCol.g = IMG_NORM_PIXEL(inputImage, sUv).g;
      sCol.b = IMG_NORM_PIXEL(inputImage, sUv - cd).b;
    } else {
      sCol = IMG_NORM_PIXEL(inputImage, sUv).rgb;
    }
    // Weight by closeness of source pixel depth to slice depth
    float pixDepth = pow(luma(sCol), max(0.1, uDepthBoost));
    float w = exp(-pow((pixDepth - sliceDepth) * 4.0, 2.0));
    acc += sCol * w;
    wsum += w;
  }
  vec3 result = (wsum > 0.0001) ? acc / wsum : src;
  gl_FragColor = vec4(result, 1.0);
}
