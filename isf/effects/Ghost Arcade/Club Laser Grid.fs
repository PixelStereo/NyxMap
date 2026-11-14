/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Perspective grid (floor / ceiling / tunnel) with audio-reactive scanlines and glowing intersections. Adapted from Ghost Arcade's Club Laser Grid effect.",
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
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "gridDensity",
            "LABEL": "Grid Density",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
        },
        {
            "NAME": "perspectiveValue",
            "LABEL": "Perspective",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "intersectionGlow",
            "LABEL": "Intersection Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "lineWidth",
            "LABEL": "Line Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.5
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (clubLaserGridHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (intensity)
#define uGridDensity (gridDensity)
#define uPerspective (perspectiveValue)
#define uSpeed (speed)
#define uIntersectionGlow (intersectionGlow)
#define uLineWidth (lineWidth)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uAudio 0.0
#define uAudioReact 0.7
#define uMode float(floor(modeValue + 0.5))

     // 0-2
   // 4-32
   // 0-1 fake 3D depth
         // 0-3
 // 0-1
     // 0.5-4




    // 0-2
          // 0=floor grid, 1=ceiling, 2=tunnel

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  int mode = int(uMode + 0.5);

  // Perspective transform
  vec2 uv = vUv;
  if (mode == 0) {
    // Floor: bottom is closer, top is further
    float persp = mix(1.0, 0.2, uv.y * uPerspective);
    uv.x = (uv.x - 0.5) / persp + 0.5;
    uv.y = mix(uv.y, pow(uv.y, 2.0), uPerspective);
  } else if (mode == 1) {
    // Ceiling: inverted floor
    float persp = mix(1.0, 0.2, (1.0 - uv.y) * uPerspective);
    uv.x = (uv.x - 0.5) / persp + 0.5;
    uv.y = mix(uv.y, 1.0 - pow(1.0 - uv.y, 2.0), uPerspective);
  } else {
    // Tunnel: radial perspective
    vec2 d = uv - 0.5;
    float r = length(d);
    uv = 0.5 + d / max(0.01, r * uPerspective + (1.0 - uPerspective));
  }

  // Animated grid lines
  float t = uTime * uSpeed;
  float audioBoost = 1.0 + uAudio * uAudioReact;
  vec2 grid = abs(fract(uv * uGridDensity * audioBoost - vec2(0.0, t * 0.3)) - 0.5);
  float lineX = smoothstep(uLineWidth * 0.02, 0.0, grid.x);
  float lineY = smoothstep(uLineWidth * 0.02, 0.0, grid.y);
  float gridLine = max(lineX, lineY);

  // Intersection brightness
  float intersect = lineX * lineY * uIntersectionGlow * (1.0 + audioBoost);

  vec3 grid3 = vec3(uTintR, uTintG, uTintB) * (gridLine + intersect * 2.0) * uIntensity;
  vec3 result = 1.0 - (1.0 - src) * (1.0 - grid3);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
