/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Newspaper-style halftone dots. Adapted from Ghost Arcade's Halftone effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Stylize",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "dotSize",
            "LABEL": "Dot Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 12.0,
            "DEFAULT": 6.0
        },
        {
            "NAME": "dotShape",
            "LABEL": "Dot Shape",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "angleC",
            "LABEL": "Angle C",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 30.0,
            "DEFAULT": 15.0
        },
        {
            "NAME": "angleM",
            "LABEL": "Angle M",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 150.0,
            "DEFAULT": 75.0
        },
        {
            "NAME": "angleY",
            "LABEL": "Angle Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "angleK",
            "LABEL": "Angle K",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 90.0,
            "DEFAULT": 45.0
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
            "NAME": "driftSpeed",
            "LABEL": "Drift Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (halftoneHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uDotSize (dotSize)
#define uDotShape float(floor(dotShape + 0.5))
#define uAngleC (angleC)
#define uAngleM (angleM)
#define uAngleY (angleY)
#define uAngleK (angleK)
#define uMode float(floor(modeValue + 0.5))
#define uDriftSpeed (driftSpeed)
#define uSpotColor vec3(0.0, 0.0, 0.0)

     // 1-32 dot grid size
    // 0=round, 1=square, 2=horizontal line, 3=vertical line
      // 0-180° dot angle (cyan)
      // magenta
      // yellow
      // black
        // 0=greyscale halftone, 1=CMYK separation, 2=spot color
  // 0-2 animated grid drift speed
    // for mode=2

// Single-channel halftone — returns 0..1 dot density at this position.
float halftoneDot(vec2 pos, float angleDeg, float channelVal, float shape) {
  float a = radians(angleDeg);
  float c = cos(a);
  float s = sin(a);
  vec2 p = mat2(c, -s, s, c) * pos;
  p += vec2(uTime * uDriftSpeed * 0.5, uTime * uDriftSpeed * 0.3);
  vec2 cell = fract(p) - 0.5;
  int sh = int(shape + 0.5);
  float d;
  if (sh == 0) {
    // Round dot — radius proportional to channel value.
    d = length(cell) - mix(0.49, 0.0, channelVal);
  } else if (sh == 1) {
    // Square dot.
    d = max(abs(cell.x), abs(cell.y)) - mix(0.49, 0.0, channelVal);
  } else if (sh == 2) {
    // Horizontal line — line thickness is channel value.
    d = abs(cell.y) - mix(0.49, 0.0, channelVal) * 0.5;
  } else {
    // Vertical line.
    d = abs(cell.x) - mix(0.49, 0.0, channelVal) * 0.5;
  }
  return smoothstep(0.02, -0.02, d);
}

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  int mode = int(uMode + 0.5);
  vec2 pos = vUv * uResolution / max(uDotSize, 1.0);

  if (mode == 1) {
    // CMYK separation: convert to CMY, then use K as min of CMY.
    vec3 c = 1.0 - texColor.rgb; // approximate CMY
    float k = min(c.r, min(c.g, c.b));
    vec3 cmy = (c - k) / max(1.0 - k, 0.001);
    float dotC = halftoneDot(pos, uAngleC, cmy.r, uDotShape);
    float dotM = halftoneDot(pos, uAngleM, cmy.g, uDotShape);
    float dotY = halftoneDot(pos, uAngleY, cmy.b, uDotShape);
    float dotK = halftoneDot(pos, uAngleK, k, uDotShape);
    // Convert back to RGB by subtracting CMYK from white.
    vec3 col = vec3(1.0)
             - vec3(dotC, 0.0, 0.0) * vec3(0.0, 1.0, 1.0)
             - vec3(0.0, dotM, 0.0) * vec3(1.0, 0.0, 1.0)
             - vec3(0.0, 0.0, dotY) * vec3(1.0, 1.0, 0.0)
             - vec3(dotK);
    gl_FragColor = vec4(clamp(col, 0.0, 1.0), texColor.a);
    return;
  }

  if (mode == 2) {
    // Spot color — single dot stamp at uSpotColor over white.
    float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
    float dotV = halftoneDot(pos, uAngleK, 1.0 - lum, uDotShape);
    gl_FragColor = vec4(mix(vec3(1.0), uSpotColor, dotV), texColor.a);
    return;
  }

  // Greyscale halftone — single dot density from luma.
  float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
  float dotV = halftoneDot(pos, uAngleK, 1.0 - lum, uDotShape);
  gl_FragColor = vec4(vec3(1.0 - dotV), texColor.a);
}
