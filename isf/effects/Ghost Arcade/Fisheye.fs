/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Barrel/pincushion lens distortion. Adapted from Ghost Arcade's Fisheye effect.",
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
            "NAME": "fisheyeStrength",
            "LABEL": "Strength",
            "TYPE": "float",
            "MIN": -1.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "radiusValue",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
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
            "NAME": "zoomValue",
            "LABEL": "Zoom",
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
            "NAME": "chromaEdge",
            "LABEL": "Chroma Edge",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (fisheyeHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uStrength (fisheyeStrength)
#define uRadius (radiusValue)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uZoom (zoomValue)
#define uMode float(floor(modeValue + 0.5))
#define uChromaEdge (chromaEdge)

      // -1..1 (negative = pincushion)
        // 0.1-1
       // 0-1
       // 0-1
          // 0.5-2
          // 0=spherize, 1=barrel, 2=pincushion
    // 0-1 edge fringing

vec2 fisheyeMap(vec2 uv, float strength, float radius, float zoom) {
  vec2 d = uv - vec2(uCenterX, uCenterY);
  d.x *= uResolution.x / uResolution.y;
  float r = length(d);
  float radNorm = clamp(r / radius, 0.0, 1.0);
  int mode = int(uMode + 0.5);
  float distort;
  if (mode == 0) {
    // Spherize (smooth)
    distort = sin(radNorm * 1.5707963 * sign(strength)) * abs(strength);
  } else if (mode == 1) {
    // Barrel (cubic)
    distort = radNorm * radNorm * abs(strength) * sign(strength);
  } else {
    // Pincushion (always pulls in)
    distort = -radNorm * radNorm * abs(strength);
  }
  float scale = 1.0 + distort;
  d /= max(0.0001, scale);
  d /= zoom;
  d.x *= uResolution.y / uResolution.x;
  return d + vec2(uCenterX, uCenterY);
}

void main() {
  vec2 uvBase = fisheyeMap(vUv, uStrength, uRadius, uZoom);
  vec3 col;
  if (uChromaEdge > 0.001) {
    vec2 d = vUv - vec2(uCenterX, uCenterY);
    float r = length(d);
    float edgeAmp = smoothstep(uRadius * 0.4, uRadius, r) * uChromaEdge * 0.04;
    vec2 dir = (r > 0.001) ? d / r : vec2(1.0, 0.0);
    vec2 uvR = fisheyeMap(vUv + dir * edgeAmp, uStrength, uRadius, uZoom);
    vec2 uvB = fisheyeMap(vUv - dir * edgeAmp, uStrength, uRadius, uZoom);
    col.r = IMG_NORM_PIXEL(inputImage, clamp(uvR, vec2(0.0), vec2(1.0))).r;
    col.g = IMG_NORM_PIXEL(inputImage, clamp(uvBase, vec2(0.0), vec2(1.0))).g;
    col.b = IMG_NORM_PIXEL(inputImage, clamp(uvB, vec2(0.0), vec2(1.0))).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, clamp(uvBase, vec2(0.0), vec2(1.0))).rgb;
  }
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(col, src.a);
}
