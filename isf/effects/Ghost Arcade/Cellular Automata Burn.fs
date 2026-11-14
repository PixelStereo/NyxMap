/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Game-of-life cells spread through bright regions. Conway / Brian's Brain / Burn rules with custom thresholds. Adapted from Ghost Arcade's Cellular Automata Burn effect.",
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
            "NAME": "cellSize",
            "LABEL": "Cell Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 4.0,
            "DEFAULT": 2.0
        },
        {
            "NAME": "birthThreshold",
            "LABEL": "Birth Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "survivalLow",
            "LABEL": "Survival Low",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.5
        },
        {
            "NAME": "survivalHigh",
            "LABEL": "Survival High",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 7.0,
            "DEFAULT": 3.5
        },
        {
            "NAME": "colorR",
            "LABEL": "Color R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "colorG",
            "LABEL": "Color G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "colorB",
            "LABEL": "Color B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
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
            "NAME": "mixValue",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
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

// Ported from Ghost Arcade (cellularAutomataBurnHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uCellSize (cellSize)
#define uBirthThreshold (birthThreshold)
#define uSurvivalLow (survivalLow)
#define uSurvivalHigh (survivalHigh)
#define uColorR (colorR)
#define uColorG (colorG)
#define uColorB (colorB)
#define uMode float(floor(modeValue + 0.5))
#define uMix (mixValue)
#define uHasFeedback (FRAMEINDEX > 0 ? 1.0 : 0.0)

      // 1-8 px
// 0-1 luma threshold for new cell
   // 0-8 neighbor count low end
  // 0-8 high end



          // 0=Conway, 1=Brian's Brain, 2=Burn
           // 0-1

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void ghostArcadeEffect() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec2 texel = vec2(uCellSize) / uResolution;

  float alive = 0.0;
  if (uHasFeedback > 0.5) {
    // Sample 8 neighbors
    float n = 0.0;
    for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
        if (x == 0 && y == 0) continue;
        n += IMG_NORM_PIXEL(feedbackBuffer, vUv + texel * vec2(float(x), float(y))).r;
      }
    }
    float self = IMG_NORM_PIXEL(feedbackBuffer, vUv).r;
    int mode = int(uMode + 0.5);
    if (mode == 0) {
      // Conway: birth on 3, survive on 2-3
      if (self > 0.5 && n >= uSurvivalLow && n <= uSurvivalHigh) alive = 1.0;
      else if (self < 0.5 && n >= 2.5 && n <= 3.5) alive = 1.0;
    } else if (mode == 1) {
      // Brian's Brain: alive→dying→dead, only birth on 2
      if (self < 0.4 && n >= 1.5 && n <= 2.5) alive = 1.0;
      else if (self > 0.5) alive = 0.5; // dying
    } else {
      // Burn: cells expand into bright neighbors, decay
      if (self > 0.5) alive = self * 0.92;
      else if (n > 0.5 && luma(src) > uBirthThreshold) alive = 1.0;
    }
  } else {
    // First frame: seed from luma
    alive = step(uBirthThreshold, luma(src));
  }

  // Inject birth from current source brightness
  if (luma(src) > uBirthThreshold + 0.2) alive = max(alive, 1.0);

  vec3 cellColor = vec3(uColorR, uColorG, uColorB) * alive;
  vec3 disp = mix(src, src + cellColor, uMix);
  // Encode alive in r-channel for next frame, color in display
  gl_FragColor = vec4(alive, alive * 0.5, alive * 0.25, luma(disp));
}

void main() {
    if (PASSINDEX == 0) ghostArcadeEffect();
    else gl_FragColor = IMG_NORM_PIXEL(feedbackBuffer, isf_FragNormCoord);
}
