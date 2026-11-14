/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Render image as 0s and 1s grid. Adapted from Ghost Arcade's Binary Code effect.",
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
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "speedValue",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "cellSize",
            "LABEL": "Cell Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
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
            "DEFAULT": 0.3
        },
        {
            "NAME": "bgMix",
            "LABEL": "Bg Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "contrast",
            "LABEL": "Contrast",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (binaryCodeHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uDensity (density)
#define uSpeed (speedValue)
#define uCellSize (cellSize)
#define uColorR (colorR)
#define uColorG (colorG)
#define uColorB (colorB)
#define uBgMix (bgMix)
#define uContrast (contrast)

       // 0-1
         // 0-3
      // 6-32



         // 0-1
      // 0-2 source pixel modulates char visibility

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// Crude '0' / '1' mask
float charZero(vec2 uv) {
  vec2 c = uv - 0.5;
  float r = length(c * vec2(1.0, 0.7));
  return smoothstep(0.42, 0.38, r) - smoothstep(0.30, 0.26, r);
}
float charOne(vec2 uv) {
  vec2 c = uv - 0.5;
  float bar = step(abs(c.x + 0.05), 0.05) * step(abs(c.y), 0.4);
  float foot = step(abs(c.y + 0.4), 0.05) * step(abs(c.x), 0.2);
  return max(bar, foot);
}

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec2 px = vUv * uResolution / uCellSize;
  vec2 col = floor(px);
  vec2 cellUv = fract(px);
  float t = uTime * uSpeed;
  // Each column scrolls up at its own speed
  float colSeed = hash21(vec2(col.x, 0.0));
  float yOff = floor(t + colSeed * 50.0);
  float charSeed = hash21(vec2(col.x, col.y + yOff));
  float bit = step(0.5, charSeed);

  float charMask = bit > 0.5 ? charOne(cellUv) : charZero(cellUv);
  float spawn = step(1.0 - uDensity, hash21(col + yOff * 0.137));
  charMask *= spawn;
  // Tie character brightness to underlying source luma
  float srcL = luma(IMG_NORM_PIXEL(inputImage, (col + 0.5) * uCellSize / uResolution).rgb);
  charMask *= mix(1.0, srcL, uContrast * 0.5);

  vec3 charColor = vec3(uColorR, uColorG, uColorB) * charMask;
  vec3 result = mix(charColor, src + charColor, uBgMix);
  gl_FragColor = vec4(result, 1.0);
}
