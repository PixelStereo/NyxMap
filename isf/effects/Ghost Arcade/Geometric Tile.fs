/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Image tiles independently rotate in 3D like a flip-board display. Adapted from Ghost Arcade's Geometric Tile effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced 3D",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "tiles",
            "LABEL": "Tiles",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
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
            "NAME": "rotation",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 180.0,
            "DEFAULT": 90.0
        },
        {
            "NAME": "offsetX",
            "LABEL": "Offset X",
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

// Ported from Ghost Arcade (geometricTileHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uTiles (tiles)
#define uMode float(floor(modeValue + 0.5))
#define uRotation (rotation)
#define uOffsetX (offsetX)
#define uMix (mixValue)

         // 1-16
          // 0=mirror, 1=rotate, 2=tile, 3=quilt
      // 0-360
       // 0-1 per-row offset
           // 0-1

void main() {
  int mode = int(uMode + 0.5);
  vec2 t = vUv * uTiles;
  vec2 cell = floor(t);
  vec2 cellUv = fract(t);

  if (mode == 0) {
    // Mirror — flip alternating
    if (mod(cell.x, 2.0) > 0.5) cellUv.x = 1.0 - cellUv.x;
    if (mod(cell.y, 2.0) > 0.5) cellUv.y = 1.0 - cellUv.y;
  } else if (mode == 1) {
    // Rotate — alternate cells rotated
    float rot = mod(cell.x + cell.y, 2.0) * radians(uRotation);
    vec2 d = cellUv - 0.5;
    float c = cos(rot), s = sin(rot);
    cellUv = vec2(d.x * c - d.y * s, d.x * s + d.y * c) + 0.5;
  } else if (mode == 2) {
    // Tile — straight repeat with row offset
    cellUv.x += mod(cell.y, 2.0) * uOffsetX;
    cellUv = fract(cellUv);
  } else {
    // Quilt — mix of mirror + rotate
    if (mod(cell.x, 2.0) > 0.5) cellUv.x = 1.0 - cellUv.x;
    float rot = mod(cell.y, 2.0) * radians(uRotation);
    vec2 d = cellUv - 0.5;
    float c = cos(rot), s = sin(rot);
    cellUv = vec2(d.x * c - d.y * s, d.x * s + d.y * c) + 0.5;
  }
  cellUv = clamp(cellUv, vec2(0.0), vec2(1.0));
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  vec4 tiled = IMG_NORM_PIXEL(inputImage, cellUv);
  gl_FragColor = vec4(mix(src.rgb, tiled.rgb, uMix), src.a);
}
