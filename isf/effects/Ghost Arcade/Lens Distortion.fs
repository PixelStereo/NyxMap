/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Radial lens barrel/pincushion warp. Adapted from Ghost Arcade's Lens Distortion effect.",
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
            "NAME": "amount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
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
            "NAME": "cubicValue",
            "LABEL": "Cubic",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "anamorphicX",
            "LABEL": "Anamorphic X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.6,
            "DEFAULT": 1.3
        },
        {
            "NAME": "edgeFade",
            "LABEL": "Edge Fade",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "chromaFringe",
            "LABEL": "Chroma Fringe",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (lensDistortionHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uMode float(floor(modeValue + 0.5))
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uCubic (cubicValue)
#define uAnamorphicX (anamorphicX)
#define uEdgeFade (edgeFade)
#define uChromaFringe (chromaFringe)

        // -1..1
          // 0=barrel, 1=pincushion, 2=mustache, 3=anamorphic


         // -0.5..0.5 cubic term (mustache uses both)
   // 0.5-2 horizontal stretch
      // 0-1 fade at edges (transparent border)
  // 0-1

vec2 lensMap(vec2 uv, float k1, float k2) {
  vec2 c = vec2(uCenterX, uCenterY);
  vec2 d = uv - c;
  d.x *= uResolution.x / uResolution.y;
  float r2 = dot(d, d);
  float factor = 1.0 + k1 * r2 + k2 * r2 * r2;
  d *= factor;
  d.x *= uResolution.y / uResolution.x;
  return d + c;
}

void main() {
  int mode = int(uMode + 0.5);
  float k1, k2;
  if (mode == 0) { k1 = uAmount; k2 = 0.0; }
  else if (mode == 1) { k1 = -uAmount; k2 = 0.0; }
  else if (mode == 2) { k1 = uAmount; k2 = uCubic; }
  else { k1 = 0.0; k2 = 0.0; }

  vec2 uv;
  if (mode == 3) {
    // Anamorphic stretch — no radial, just X scale
    vec2 c = vec2(uCenterX, uCenterY);
    vec2 d = vUv - c;
    d.x /= max(0.1, uAnamorphicX);
    uv = d + c;
  } else {
    uv = lensMap(vUv, k1, k2);
  }

  vec3 col;
  if (uChromaFringe > 0.001) {
    vec2 uvR = lensMap(vUv, k1 * (1.0 + uChromaFringe * 0.05), k2);
    vec2 uvB = lensMap(vUv, k1 * (1.0 - uChromaFringe * 0.05), k2);
    col.r = IMG_NORM_PIXEL(inputImage, clamp(uvR, vec2(0.0), vec2(1.0))).r;
    col.g = IMG_NORM_PIXEL(inputImage, clamp(uv, vec2(0.0), vec2(1.0))).g;
    col.b = IMG_NORM_PIXEL(inputImage, clamp(uvB, vec2(0.0), vec2(1.0))).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
  }

  // Edge fade — transparent border for OOB samples
  float oob = step(uv.x, 0.0) + step(1.0, uv.x) + step(uv.y, 0.0) + step(1.0, uv.y);
  oob = min(oob, 1.0);
  float aFade = mix(1.0, 0.0, oob * uEdgeFade);

  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(col, src.a * aFade);
}
