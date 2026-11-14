/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Frame-difference motion-vector estimation displaces the previous frame. Normal/glitch/smooth modes with freeze + chroma split. Adapted from Ghost Arcade's Optical Flow Datamosh effect.",
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
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "motionScale",
            "LABEL": "Motion Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "persistenceValue",
            "LABEL": "Persistence",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
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
            "NAME": "blockSize",
            "LABEL": "Block Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
        },
        {
            "NAME": "freezeValue",
            "LABEL": "Freeze",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
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

// Ported from Ghost Arcade (opticalFlowDatamoshHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (intensity)
#define uMotionScale (motionScale)
#define uPersistence (persistenceValue)
#define uChromaSplit (chromaSplit)
#define uBlockSize (blockSize)
#define uFreeze (freezeValue)
#define uMode float(floor(modeValue + 0.5))
#define uHasFeedback (FRAMEINDEX > 0 ? 1.0 : 0.0)

     // 0-1
   // 0-2 displacement magnitude
   // 0-1 how much old frame survives
   // 0-1
     // 4-32 motion-block size
        // 0-1 freeze + repeat motion (no new I-frames)
          // 0=normal, 1=glitch, 2=smooth

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

vec2 estimateFlow(vec2 uv) {
  if (uHasFeedback < 0.5) return vec2(0.0);
  vec2 cell = floor(uv * uResolution / uBlockSize);
  vec2 cellOrigin = (cell + 0.5) * uBlockSize / uResolution;
  vec2 texel = 1.0 / uResolution;

  vec3 cur = IMG_NORM_PIXEL(inputImage, cellOrigin).rgb;
  float bestErr = 1e9;
  vec2 bestOff = vec2(0.0);
  // Search 5x5 block grid for best match in feedback frame
  for (int y = -2; y <= 2; y++) {
    for (int x = -2; x <= 2; x++) {
      vec2 off = vec2(float(x), float(y)) * texel * uBlockSize * 0.5;
      vec3 prev = IMG_NORM_PIXEL(feedbackBuffer, cellOrigin + off).rgb;
      float err = dot(cur - prev, cur - prev);
      if (err < bestErr) { bestErr = err; bestOff = off; }
    }
  }
  return bestOff;
}

void ghostArcadeEffect() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uHasFeedback < 0.5 || uIntensity < 0.001) { gl_FragColor = src; return; }

  int mode = int(uMode + 0.5);
  vec2 flow = estimateFlow(vUv) * uMotionScale * (mode == 1 ? 2.0 : 1.0);

  // Sample previous frame at flow-shifted UV
  vec3 prevSampled;
  if (uChromaSplit > 0.001) {
    prevSampled.r = IMG_NORM_PIXEL(feedbackBuffer, vUv + flow * (1.0 + uChromaSplit * 0.4)).r;
    prevSampled.g = IMG_NORM_PIXEL(feedbackBuffer, vUv + flow).g;
    prevSampled.b = IMG_NORM_PIXEL(feedbackBuffer, vUv + flow * (1.0 - uChromaSplit * 0.4)).b;
  } else {
    prevSampled = IMG_NORM_PIXEL(feedbackBuffer, vUv + flow).rgb;
  }

  // Mix: persistence blends old frame; freeze suppresses new frame
  vec3 newFrame = mix(src.rgb, vec3(luma(src.rgb)), uFreeze * 0.4);
  if (uFreeze > 0.001) newFrame = mix(newFrame, prevSampled, uFreeze);

  vec3 result;
  if (mode == 2) {
    // Smooth: time-blend
    result = mix(newFrame, prevSampled, uPersistence);
  } else {
    // Normal/glitch: hard mosh
    result = mix(newFrame, prevSampled, uPersistence * uIntensity);
  }

  gl_FragColor = vec4(result, src.a);
}

void main() {
    if (PASSINDEX == 0) ghostArcadeEffect();
    else gl_FragColor = IMG_NORM_PIXEL(feedbackBuffer, isf_FragNormCoord);
}
