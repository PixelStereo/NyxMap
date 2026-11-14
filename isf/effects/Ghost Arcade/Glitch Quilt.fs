/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Tiles reorder, rotate, and delay independently. Quilt / swap / mosh modes with feedback-driven delay. Adapted from Ghost Arcade's Glitch Quilt effect.",
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
            "NAME": "tileSize",
            "LABEL": "Tile Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 64.0,
            "DEFAULT": 32.0
        },
        {
            "NAME": "shuffleAmount",
            "LABEL": "Shuffle Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "rotateAmount",
            "LABEL": "Rotate Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "delayAmount",
            "LABEL": "Delay Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "chromaSplit",
            "LABEL": "Chroma Split",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "triggerRate",
            "LABEL": "Trigger Rate",
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
        }
    ],
    "PASSES": [
        {
            "TARGET": "feedbackBuffer",
            "PERSISTENT": true,
            "FLOAT": true
        },
        {}
    ]
}*/

// Ported from Ghost Arcade (glitchQuiltHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uTileSize (tileSize)
#define uShuffleAmount (shuffleAmount)
#define uRotateAmount (rotateAmount)
#define uDelayAmount (delayAmount)
#define uChromaSplit (chromaSplit)
#define uTriggerRate (triggerRate)
#define uMode float(floor(modeValue + 0.5))
#define uHasFeedback (FRAMEINDEX > 0 ? 1.0 : 0.0)

      // 8-128
 // 0-1
  // 0-1
   // 0-1 (only with feedback)
   // 0-1
   // 0-3 reshuffle frequency
          // 0=quilt, 1=swap, 2=mosh

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void ghostArcadeEffect() {
  vec2 cell = floor(vUv * uResolution / uTileSize);
  vec2 cellOrigin = cell * uTileSize / uResolution;
  vec2 cellSize = vec2(uTileSize) / uResolution;
  vec2 cellUv = (vUv - cellOrigin) / cellSize;

  // Per-cell randomness, refreshed at trigger rate
  float t = floor(uTime * uTriggerRate * 2.0);
  float h = hash21(cell + t);
  float h2 = hash21(cell + t + 13.7);

  // Shuffle: replace this tile with one elsewhere
  vec2 srcCell = cell;
  if (h < uShuffleAmount) {
    srcCell.x = floor(hash21(cell + t + 7.3) * uResolution.x / uTileSize);
    srcCell.y = floor(hash21(cell + t + 17.5) * uResolution.y / uTileSize);
  }

  // Rotate: per-cell quad rotation
  vec2 sUv = cellUv;
  if (h2 < uRotateAmount) {
    int rot = int(hash21(cell + t + 27.3) * 4.0);
    if (rot == 1) sUv = vec2(sUv.y, 1.0 - sUv.x);
    else if (rot == 2) sUv = vec2(1.0 - sUv.x, 1.0 - sUv.y);
    else if (rot == 3) sUv = vec2(1.0 - sUv.y, sUv.x);
  }
  vec2 finalUv = (srcCell * uTileSize + sUv * uTileSize) / uResolution;
  finalUv = clamp(finalUv, vec2(0.0), vec2(1.0));

  int mode = int(uMode + 0.5);
  vec3 col;
  if (uChromaSplit > 0.001) {
    vec2 cd = vec2(uChromaSplit * 0.005, 0.0);
    col.r = IMG_NORM_PIXEL(inputImage, finalUv + cd).r;
    col.g = IMG_NORM_PIXEL(inputImage, finalUv).g;
    col.b = IMG_NORM_PIXEL(inputImage, finalUv - cd).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, finalUv).rgb;
  }

  // Delay: blend with feedback
  if (uDelayAmount > 0.001 && uHasFeedback > 0.5) {
    float useFb = step(0.5, hash21(cell + 71.3));
    vec3 prev = IMG_NORM_PIXEL(feedbackBuffer, finalUv).rgb;
    col = mix(col, prev, useFb * uDelayAmount);
  }

  gl_FragColor = vec4(col, 1.0);
}

void main() {
    if (PASSINDEX == 0) ghostArcadeEffect();
    else gl_FragColor = IMG_NORM_PIXEL(feedbackBuffer, isf_FragNormCoord);
}
