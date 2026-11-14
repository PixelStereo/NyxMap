/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Kaleidoscopic shard field (voronoi / hex / triangular) with per-shard rotation and feedback delay. Adapted from Ghost Arcade's Mirror Shards effect.",
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
            "NAME": "shardsValue",
            "LABEL": "Shards",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 16.0,
            "DEFAULT": 8.0
        },
        {
            "NAME": "shardSize",
            "LABEL": "Shard Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "rotationValue",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 120.0,
            "DEFAULT": 60.0
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
            "NAME": "chromatic",
            "LABEL": "Chromatic",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
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

// Ported from Ghost Arcade (mirrorShardsHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uShards (shardsValue)
#define uShardSize (shardSize)
#define uRotation (rotationValue)
#define uDelayAmount (delayAmount)
#define uChromatic (chromatic)
#define uMode float(floor(modeValue + 0.5))
#define uHasFeedback (FRAMEINDEX > 0 ? 1.0 : 0.0)

        // 4-32
     // 0.05-0.5
      // 0-360 max per-shard rotation
   // 0-1 (uses feedback)
     // 0-1
          // 0=voronoi, 1=hex, 2=triangular

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void ghostArcadeEffect() {
  int mode = int(uMode + 0.5);
  vec2 cellId; vec2 cellCenter; vec2 cellUv;

  if (mode == 0) {
    // Voronoi shards
    vec2 p = vUv * uShards;
    vec2 i = floor(p), f = fract(p);
    float minD = 9.0;
    vec2 best = vec2(0.0);
    for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
        vec2 g = vec2(float(x), float(y));
        vec2 o = vec2(hash21(i + g), hash21(i + g + 13.7));
        vec2 r = g + o - f;
        float d = dot(r, r);
        if (d < minD) { minD = d; best = i + g; cellUv = r; }
      }
    }
    cellId = best;
    cellCenter = (best + 0.5) / uShards;
  } else if (mode == 1) {
    // Hex
    vec2 q = vec2(vUv.x * 1.1547, vUv.y) * uShards;
    q.x += 0.5 * floor(q.y);
    vec2 i = floor(q);
    cellId = vec2(i.x - floor(i.y / 2.0), i.y);
    cellCenter = (cellId + 0.5) / uShards;
    cellUv = fract(q) - 0.5;
  } else {
    // Triangular
    vec2 p = vUv * uShards;
    vec2 i = floor(p);
    cellId = i;
    cellCenter = (i + 0.5) / uShards;
    cellUv = fract(p) - 0.5;
  }

  // Per-shard rotation + delay
  float ang = (hash21(cellId) - 0.5) * radians(uRotation);
  ang += uTime * 0.1 * (hash21(cellId + 7.3) - 0.5);
  float ca = cos(ang), sa = sin(ang);
  vec2 sUv = cellCenter + vec2(cellUv.x * ca - cellUv.y * sa, cellUv.x * sa + cellUv.y * ca) * uShardSize;
  sUv = clamp(sUv, vec2(0.0), vec2(1.0));

  vec3 col;
  if (uChromatic > 0.001) {
    vec2 cd = vec2(uChromatic * 0.005, 0.0);
    col.r = IMG_NORM_PIXEL(inputImage, sUv + cd).r;
    col.g = IMG_NORM_PIXEL(inputImage, sUv).g;
    col.b = IMG_NORM_PIXEL(inputImage, sUv - cd).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, sUv).rgb;
  }

  // Delay with feedback (random per-shard mix)
  if (uDelayAmount > 0.001 && uHasFeedback > 0.5) {
    float useFb = step(0.5, hash21(cellId + 71.3));
    vec3 prev = IMG_NORM_PIXEL(feedbackBuffer, sUv).rgb;
    col = mix(col, prev, useFb * uDelayAmount);
  }

  gl_FragColor = vec4(col, 1.0);
}

void main() {
    if (PASSINDEX == 0) ghostArcadeEffect();
    else gl_FragColor = IMG_NORM_PIXEL(feedbackBuffer, isf_FragNormCoord);
}
