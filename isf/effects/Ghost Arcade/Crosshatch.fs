/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Pen-and-ink crosshatch shading. Adapted from Ghost Arcade's Crosshatch effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Text & Pattern",
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
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 60.0,
            "DEFAULT": 30.0
        },
        {
            "NAME": "lineWidth",
            "LABEL": "Line Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "contrast",
            "LABEL": "Contrast",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "paperR",
            "LABEL": "Paper R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.9,
            "DEFAULT": 0.95
        },
        {
            "NAME": "paperG",
            "LABEL": "Paper G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.86,
            "DEFAULT": 0.93
        },
        {
            "NAME": "paperB",
            "LABEL": "Paper B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.76,
            "DEFAULT": 0.88
        },
        {
            "NAME": "inkR",
            "LABEL": "Ink R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
        },
        {
            "NAME": "inkG",
            "LABEL": "Ink G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
        },
        {
            "NAME": "inkB",
            "LABEL": "Ink B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
        }
    ]
}*/

// Ported from Ghost Arcade (crosshatchHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uDensity (density)
#define uAngle (angle)
#define uLineWidth (lineWidth)
#define uContrast (contrast)
#define uPaperR (paperR)
#define uPaperG (paperG)
#define uPaperB (paperB)
#define uInkR (inkR)
#define uInkG (inkG)
#define uInkB (inkB)

       // 0-1
         // 0-180
     // 0.5-4
      // 0-2

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

float hatchLine(vec2 uv, float ang, float spacing, float width) {
  float c = cos(ang); float s = sin(ang);
  float v = uv.x * c + uv.y * s;
  return smoothstep(width, width * 0.5, abs(fract(v / spacing) - 0.5));
}

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  float l = luma(src);
  l = clamp((l - 0.5) * uContrast + 0.5, 0.0, 1.0);

  vec2 px = vUv * uResolution;
  float spacing = 8.0 / uDensity;
  float w = 0.5 / spacing * max(0.5, uLineWidth);
  float baseAng = radians(uAngle);

  // Build cross-hatching
  float h = 0.0;
  if (l < 0.85) h = max(h, hatchLine(px, baseAng,            spacing,       w));
  if (l < 0.65) h = max(h, hatchLine(px, baseAng + 1.5708,   spacing * 0.9, w));
  if (l < 0.45) h = max(h, hatchLine(px, baseAng + 0.7854,   spacing * 0.8, w));
  if (l < 0.25) h = max(h, hatchLine(px, baseAng + 2.3562,   spacing * 0.7, w));

  vec3 paper = vec3(uPaperR, uPaperG, uPaperB);
  vec3 ink = vec3(uInkR, uInkG, uInkB);
  vec3 result = mix(paper, ink, h);
  gl_FragColor = vec4(result, 1.0);
}
