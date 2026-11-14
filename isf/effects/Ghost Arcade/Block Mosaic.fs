/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Unicode-style block element rendering (light/medium/dark/full). Adapted from Ghost Arcade's Block Mosaic effect.",
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
            "NAME": "tileSize",
            "LABEL": "Tile Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 48.0,
            "DEFAULT": 24.0
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
            "NAME": "groutValue",
            "LABEL": "Grout",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.15
        },
        {
            "NAME": "colorJitter",
            "LABEL": "Color Jitter",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
        },
        {
            "NAME": "groutR",
            "LABEL": "Grout R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
        },
        {
            "NAME": "groutG",
            "LABEL": "Grout G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
        },
        {
            "NAME": "groutB",
            "LABEL": "Grout B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
        }
    ]
}*/

// Ported from Ghost Arcade (blockMosaicHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uTileSize (tileSize)
#define uMode float(floor(modeValue + 0.5))
#define uGrout (groutValue)
#define uColorJitter (colorJitter)
#define uGroutR (groutR)
#define uGroutG (groutG)
#define uGroutB (groutB)

      // 8-64
          // 0=square, 1=voronoi, 2=hex, 3=brick
         // 0-1
   // 0-1

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
  int mode = int(uMode + 0.5);
  vec2 px = vUv * uResolution / uTileSize;
  vec2 cell;
  vec2 cellUv;
  if (mode == 0) {
    cell = floor(px);
    cellUv = fract(px) - 0.5;
  } else if (mode == 1) {
    vec2 i = floor(px);
    vec2 f = fract(px);
    vec2 best = vec2(0.0);
    float minD = 9.0;
    for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
        vec2 g = vec2(float(x), float(y));
        vec2 o = vec2(hash21(i + g), hash21(i + g + 13.7));
        vec2 r = g + o - f;
        float d = dot(r, r);
        if (d < minD) { minD = d; best = i + g; cellUv = r; }
      }
    }
    cell = best;
  } else if (mode == 2) {
    // Hex
    vec2 q = vec2(px.x * 1.1547, px.y);
    q.x += 0.5 * floor(q.y);
    vec2 i = floor(q);
    cell = vec2(i.x - floor(i.y / 2.0), i.y);
    cellUv = fract(q) - 0.5;
  } else {
    // Brick
    vec2 q = px;
    float row = floor(q.y);
    q.x += mod(row, 2.0) * 0.5;
    cell = vec2(floor(q.x), row);
    cellUv = fract(q) - 0.5;
  }

  vec2 cellCenter = (cell + 0.5) * uTileSize / uResolution;
  vec3 tileCol = IMG_NORM_PIXEL(inputImage, cellCenter).rgb;

  // Grout band
  float dist = (mode == 1) ? sqrt(length(cellUv)) : max(abs(cellUv.x), abs(cellUv.y));
  float grout = step(0.5 - uGrout * 0.5, dist);

  // Color jitter
  if (uColorJitter > 0.001) {
    vec3 j = vec3(hash21(cell), hash21(cell + 7.3), hash21(cell + 13.7)) - 0.5;
    tileCol += j * uColorJitter * 0.4;
  }
  vec3 groutCol = vec3(uGroutR, uGroutG, uGroutB);
  vec3 result = mix(tileCol, groutCol, grout);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
