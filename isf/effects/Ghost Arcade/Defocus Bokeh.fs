/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Hexagonal bokeh-shaped out-of-focus blur. Adapted from Ghost Arcade's Defocus Bokeh effect.",
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
            "NAME": "radiusValue",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
        },
        {
            "NAME": "samplesValue",
            "LABEL": "Samples",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 48.0,
            "DEFAULT": 24.0
        },
        {
            "NAME": "brightWeight",
            "LABEL": "Bright Weight",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.6,
            "DEFAULT": 0.8
        },
        {
            "NAME": "thresholdValue",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "chromaFringe",
            "LABEL": "Chroma Fringe",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "shapeValue",
            "LABEL": "Shape",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "rotationValue",
            "LABEL": "Rotation",
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

// Ported from Ghost Arcade (defocusBokehHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uRadius (radiusValue)
#define uSamples (samplesValue)
#define uBrightWeight (brightWeight)
#define uThreshold (thresholdValue)
#define uChromaFringe (chromaFringe)
#define uShape float(floor(shapeValue + 0.5))
#define uRotation (rotationValue)
#define uMix (mixValue)

        // 0-30 disc radius
       // 12-48
  // 0-2 boost on highlights (creates bokeh balls)
     // 0-1 highlight threshold
  // 0-1 RGB radial offset
         // 0=disc, 1=hexagon, 2=octagon
      // 0-360 aperture rotation
           // 0-1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// Aperture mask: returns 1 if (sx, sy) is inside the aperture shape
float apertureMask(vec2 p, int shape) {
  float r = length(p);
  if (r > 1.0) return 0.0;
  if (shape == 0) return 1.0; // disc
  float ang = atan(p.y, p.x);
  int sides = (shape == 1) ? 6 : 8;
  float n = float(sides);
  float halfAng = 3.14159 / n;
  float folded = mod(ang + halfAng, 2.0 * halfAng) - halfAng;
  float polyR = cos(halfAng) / cos(folded);
  return r <= polyR ? 1.0 : 0.0;
}

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uRadius < 0.5) { gl_FragColor = src; return; }

  int samples = int(clamp(uSamples, 8.0, 48.0));
  int shape = int(uShape + 0.5);
  float rot = radians(uRotation);
  float ca = cos(rot), sa = sin(rot);
  vec2 texel = 1.0 / uResolution;

  vec3 acc = vec3(0.0);
  float wsum = 0.0;

  // Sample on golden-angle spiral inside aperture mask
  for (int i = 0; i < 48; i++) {
    if (i >= samples) break;
    float fi = float(i);
    float t = fi / float(samples);
    // Golden-angle spiral
    float angle = fi * 2.39996;
    float radius = sqrt(t);
    vec2 disc = vec2(cos(angle), sin(angle)) * radius;
    // Rotate to user-set aperture rotation
    vec2 rotDisc = vec2(disc.x * ca - disc.y * sa, disc.x * sa + disc.y * ca);
    float mask = apertureMask(rotDisc, shape);
    if (mask < 0.5) continue;

    vec2 off;
    vec3 c;
    if (uChromaFringe > 0.001) {
      // Per-channel offset for fringing
      vec2 dir = normalize(rotDisc + 1e-6);
      float rOff = uRadius * (1.0 + uChromaFringe * 0.05);
      float bOff = uRadius * (1.0 - uChromaFringe * 0.05);
      off = rotDisc * uRadius * texel;
      float r = IMG_NORM_PIXEL(inputImage, vUv + dir * rOff * texel + (off - dir * uRadius * texel)).r;
      float g = IMG_NORM_PIXEL(inputImage, vUv + off).g;
      float b = IMG_NORM_PIXEL(inputImage, vUv + dir * bOff * texel + (off - dir * uRadius * texel)).b;
      c = vec3(r, g, b);
    } else {
      off = rotDisc * uRadius * texel;
      c = IMG_NORM_PIXEL(inputImage, vUv + off).rgb;
    }

    // Bright-weight: boost highlights so they form crisp bokeh balls
    float w = 1.0;
    if (uBrightWeight > 0.001) {
      float l = luma(c);
      float hi = smoothstep(uThreshold, uThreshold + 0.2, l);
      w = mix(1.0, 1.0 + uBrightWeight * 6.0, hi);
    }
    acc += c * w;
    wsum += w;
  }

  vec3 result = (wsum > 0.0) ? acc / wsum : src.rgb;
  gl_FragColor = vec4(mix(src.rgb, result, uMix), src.a);
}
