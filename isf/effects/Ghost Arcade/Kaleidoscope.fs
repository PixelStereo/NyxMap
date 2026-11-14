/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Mirror symmetry kaleidoscope segments. Adapted from Ghost Arcade's Kaleidoscope effect.",
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
            "NAME": "kaleidoscopeSegments",
            "LABEL": "Segments",
            "TYPE": "float",
            "MIN": 2.0,
            "MAX": 16.0,
            "DEFAULT": 6.0
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
            "NAME": "spiralAmount",
            "LABEL": "Spiral Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "animSpeed",
            "LABEL": "Anim Speed",
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

// Ported from Ghost Arcade (kaleidoscopeHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uSegments (kaleidoscopeSegments)
#define uAngle (angle)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uZoom (zoomValue)
#define uMode float(floor(modeValue + 0.5))
#define uSpiralAmount (spiralAmount)
#define uAnimSpeed (animSpeed)
#define uMix (mixValue)

      // 2-32
         // 0-360 rotation
       // 0-1
       // 0-1
          // 0.25-4
          // 0=mirror, 1=tile (no flip), 2=spiral
  // 0-2 spiral twist (used in mode 2)
     // 0-2 auto-rotate
           // 0-1

void main() {
  vec2 center = vec2(uCenterX, uCenterY);
  vec2 d = vUv - center;
  // Aspect correct
  d.x *= uResolution.x / uResolution.y;
  float r = length(d);
  float theta = atan(d.y, d.x);
  float baseAngle = radians(uAngle) + uTime * uAnimSpeed * 0.3;
  theta -= baseAngle;

  int mode = int(uMode + 0.5);
  float seg = max(2.0, uSegments);
  float wedge = 6.28318 / seg;

  if (mode == 0) {
    // Mirror — fold within wedge
    theta = mod(theta, wedge);
    theta = abs(theta - wedge * 0.5);
  } else if (mode == 1) {
    // Tile — modulo without flip
    theta = mod(theta, wedge);
  } else {
    // Spiral — fold + radial twist
    theta = mod(theta, wedge);
    theta = abs(theta - wedge * 0.5);
    theta += r * uSpiralAmount * 2.0;
  }

  float zoom = max(0.05, uZoom);
  vec2 mappedD = vec2(cos(theta), sin(theta)) * r / zoom;
  mappedD.x *= uResolution.y / uResolution.x;
  vec2 mappedUv = mappedD + center;
  mappedUv = clamp(mappedUv, vec2(0.0), vec2(1.0));

  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  vec4 mapped = IMG_NORM_PIXEL(inputImage, mappedUv);
  gl_FragColor = vec4(mix(src.rgb, mapped.rgb, uMix), src.a);
}
