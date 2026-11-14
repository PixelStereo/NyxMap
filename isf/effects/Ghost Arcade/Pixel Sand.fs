/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Bright pixels fall like sand under gravity + turbulence. Fall / rise / swirl directions, persistence + replenish. Adapted from Ghost Arcade's Pixel Sand effect.",
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
            "NAME": "gravity",
            "LABEL": "Gravity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "turbulence",
            "LABEL": "Turbulence",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "threshold",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "persistence",
            "LABEL": "Persistence",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.84,
            "DEFAULT": 0.92
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
            "NAME": "replenish",
            "LABEL": "Replenish",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "chromaSplit",
            "LABEL": "Chroma Split",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "grainSize",
            "LABEL": "Grain Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 6.0,
            "DEFAULT": 3.0
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

// Ported from Ghost Arcade (pixelSandHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uGravity (gravity)
#define uTurbulence (turbulence)
#define uThreshold (threshold)
#define uPersistence (persistence)
#define uMode float(floor(modeValue + 0.5))
#define uReplenish (replenish)
#define uChromaSplit (chromaSplit)
#define uGrainSize (grainSize)
#define uHasFeedback (FRAMEINDEX > 0 ? 1.0 : 0.0)

       // 0-2 fall distance per frame
    // 0-1 random per-grain jitter
     // 0-1 luma gate (bright pixels become sand)
   // 0-1 how long sand stays before fading
          // 0=fall, 1=rise, 2=swirl
     // 0-1 how much new sand spawns each frame
   // 0-1
     // 1-6 sand grain pixel size

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void ghostArcadeEffect() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  int mode = int(uMode + 0.5);

  // Snap to grain grid so sand looks pixelated/granular (not smooth)
  vec2 grain = floor(vUv * uResolution / max(1.0, uGrainSize));
  vec2 grainPos = grain * uGrainSize / uResolution;
  vec2 grainCenter = grainPos + vec2(uGrainSize) / uResolution * 0.5;
  float grainSeed = hash21(grain);

  // Direction of motion (texture-space y down)
  vec2 dir;
  if (mode == 0) dir = vec2(0.0, -1.0);                 // fall (sample_ above)
  else if (mode == 1) dir = vec2(0.0, 1.0);             // rise
  else dir = vec2(sin(uTime * 0.4 + grainCenter.y * 6.28), 0.6); // swirl

  if (uTurbulence > 0.001) {
    vec2 jit = vec2(hash21(grain + uTime * 0.05), hash21(grain * 1.3 + uTime * 0.05 + 7.3)) - 0.5;
    dir += jit * uTurbulence;
  }

  // Sample feedback at the cell this grain "fell from"
  float fallStep = uGravity * uGrainSize / uResolution.y;
  vec2 fbUv = grainCenter - dir * fallStep;
  vec3 fallen = vec3(0.0);
  float fallenAlpha = 0.0;
  if (uHasFeedback > 0.5) {
    vec3 prev;
    if (uChromaSplit > 0.001) {
      prev.r = IMG_NORM_PIXEL(feedbackBuffer, fbUv + dir * uChromaSplit * 0.003).r;
      prev.g = IMG_NORM_PIXEL(feedbackBuffer, fbUv).g;
      prev.b = IMG_NORM_PIXEL(feedbackBuffer, fbUv - dir * uChromaSplit * 0.003).b;
    } else {
      prev = IMG_NORM_PIXEL(feedbackBuffer, fbUv).rgb;
    }
    fallen = prev * uPersistence;
    // Snap to discrete particle: only kept if luma above gate
    fallenAlpha = step(0.05, luma(prev)) * uPersistence;
  }

  // Spawn new sand: only at bright source pixels, sparsely (granular)
  vec3 sampleCol = IMG_NORM_PIXEL(inputImage, grainCenter).rgb;
  float lumaNow = luma(sampleCol);
  float sparkle = step(1.0 - uReplenish, grainSeed); // discrete spawn mask
  float spawnW = smoothstep(uThreshold, uThreshold + 0.05, lumaNow) * sparkle;
  vec3 newSand = sampleCol * spawnW;

  // Combine: take the brighter of the two (sand "wins" over old or new)
  vec3 sand = max(fallen, newSand);
  float sandAlpha = max(fallenAlpha, spawnW);

  // Composite: sand REPLACES source where present (granular look), source visible elsewhere.
  // Darken source slightly under sand for contrast.
  vec3 srcDimmed = src * (1.0 - sandAlpha * 0.4);
  vec3 disp = mix(srcDimmed, sand, sandAlpha);
  gl_FragColor = vec4(clamp(disp, 0.0, 1.0), 1.0);
}

void main() {
    if (PASSINDEX == 0) ghostArcadeEffect();
    else gl_FragColor = IMG_NORM_PIXEL(feedbackBuffer, isf_FragNormCoord);
}
