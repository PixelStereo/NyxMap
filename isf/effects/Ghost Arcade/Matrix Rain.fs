/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Falling green character columns like The Matrix. Adapted from Ghost Arcade's Matrix Rain effect.",
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
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "speedValue",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "cellSize",
            "LABEL": "Cell Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 28.0,
            "DEFAULT": 14.0
        },
        {
            "NAME": "trailLength",
            "LABEL": "Trail Length",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "colorR",
            "LABEL": "Color R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "colorG",
            "LABEL": "Color G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "colorB",
            "LABEL": "Color B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "bgMix",
            "LABEL": "Bg Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        }
    ]
}*/

// Ported from Ghost Arcade (matrixRainHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uDensity (density)
#define uSpeed (speedValue)
#define uCellSize (cellSize)
#define uTrailLength (trailLength)
#define uColorR (colorR)
#define uColorG (colorG)
#define uColorB (colorB)
#define uBgMix (bgMix)

       // 0-1
         // 0-3
      // 6-32
   // 0-1



         // 0-1 keep underlying frame

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

// Fake glyph: bit pattern within sub-grid
float glyph(vec2 cellUv, float seed) {
  vec2 g = floor(cellUv * 5.0);
  float bit = hash21(g + seed * 13.0);
  return step(0.55, bit);
}

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;

  vec2 px = vUv * uResolution / uCellSize;
  vec2 col = floor(px);
  vec2 cellUv = fract(px);
  // Column-specific speed and seed
  float colSeed = hash21(vec2(col.x, 0.0));
  float fallSpeed = (0.5 + colSeed * 1.5) * uSpeed;
  float trailHead = mod(uTime * fallSpeed - colSeed * 50.0, uResolution.y / uCellSize + 30.0);
  float dist = trailHead - col.y;

  float trailLen = max(2.0, uTrailLength * 30.0);
  float intensity = 0.0;
  if (dist > 0.0 && dist < trailLen) {
    intensity = (1.0 - dist / trailLen);
    intensity *= step(1.0 - uDensity, hash21(col + floor(uTime * fallSpeed * 0.05)));
  }
  if (dist >= 0.0 && dist < 1.0) intensity = 1.5; // bright head

  // Glyph mask (changes over time for rain feel)
  float glyphSeed = hash21(col + floor(uTime * fallSpeed * 0.5 + col.y * 0.1));
  float gMask = glyph(cellUv, glyphSeed);

  vec3 rainColor = vec3(uColorR, uColorG, uColorB) * intensity * gMask;
  vec3 result = mix(rainColor, src + rainColor, uBgMix);
  gl_FragColor = vec4(result, 1.0);
}
