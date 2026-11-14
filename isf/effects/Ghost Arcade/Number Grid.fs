/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Map luminance to 0-9 digit density patterns. Adapted from Ghost Arcade's Number Grid effect.",
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
            "NAME": "mode",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "amount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Detail",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount3",
            "LABEL": "Amount3",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "threshold",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (numberGridShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(mode + 0.5))
#define uAmount (amount)
#define uAmount2 (amount2)
#define uAmount3 (amount3)
#define uThreshold (threshold)
#define uAngle (angle)
#define uCenter vec2(0.5, 0.5)
#define uColor vec3(0.5, 0.5, 0.5)

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float cellSize = mix(6.0, 24.0, uAmount);
  float contrast = 0.5 + uAmount2 * 1.5;
  float colorMix = uAmount3;

  vec2 cell = floor(uv * uResolution / cellSize);
  vec2 cellUv = (cell + 0.5) * cellSize / uResolution;
  vec4 cellColor = IMG_NORM_PIXEL(inputImage, cellUv);
  float l = luma(cellColor.rgb);
  l = clamp((l - 0.5) * contrast + 0.5, 0.0, 1.0);

  vec2 local = fract(uv * uResolution / cellSize);

  // Map luminance to digit 0-9
  int digit = int(l * 9.99);
  float charMask = 0.0;
  float cx = local.x;
  float cy = 1.0 - local.y;

  // 3x5 grid cell index (use int to avoid float comparison issues)
  int gx = int(floor(cx * 3.0));
  int gy = int(floor(cy * 5.0));
  int gi = gy * 3 + gx;

  // Segment lookup per digit (3x5 grid encoding)
  if (digit == 0) {
    charMask = (gi==0||gi==1||gi==2||gi==3||gi==5||gi==6||gi==8||gi==9||gi==11||gi==12||gi==13||gi==14) ? 1.0 : 0.0;
  } else if (digit == 1) {
    charMask = (gi==1||gi==4||gi==7||gi==10||gi==13) ? 1.0 : 0.0;
  } else if (digit == 2) {
    charMask = (gi==0||gi==1||gi==2||gi==5||gi==6||gi==7||gi==8||gi==9||gi==12||gi==13||gi==14) ? 1.0 : 0.0;
  } else if (digit == 3) {
    charMask = (gi==0||gi==1||gi==2||gi==5||gi==6||gi==7||gi==8||gi==11||gi==12||gi==13||gi==14) ? 1.0 : 0.0;
  } else if (digit == 4) {
    charMask = (gi==0||gi==2||gi==3||gi==5||gi==6||gi==7||gi==8||gi==11||gi==14) ? 1.0 : 0.0;
  } else if (digit == 5) {
    charMask = (gi==0||gi==1||gi==2||gi==3||gi==6||gi==7||gi==8||gi==11||gi==12||gi==13||gi==14) ? 1.0 : 0.0;
  } else if (digit == 6) {
    charMask = (gi==0||gi==1||gi==2||gi==3||gi==6||gi==7||gi==8||gi==9||gi==11||gi==12||gi==13||gi==14) ? 1.0 : 0.0;
  } else if (digit == 7) {
    charMask = (gi==0||gi==1||gi==2||gi==5||gi==8||gi==11||gi==14) ? 1.0 : 0.0;
  } else if (digit == 8) {
    charMask = (gi==0||gi==1||gi==2||gi==3||gi==5||gi==6||gi==7||gi==8||gi==9||gi==11||gi==12||gi==13||gi==14) ? 1.0 : 0.0;
  } else {
    charMask = (gi==0||gi==1||gi==2||gi==3||gi==5||gi==6||gi==7||gi==8||gi==11||gi==12||gi==13||gi==14) ? 1.0 : 0.0;
  }

  // Padding: only show within inner cell area
  float inCell = step(0.1, cx) * step(cx, 0.9) * step(0.05, cy) * step(cy, 0.95);
  charMask *= inCell;

  vec3 numColor = mix(vec3(charMask), cellColor.rgb * charMask, colorMix);
  gl_FragColor = vec4(clamp(numColor, 0.0, 1.0), src.a);
}
