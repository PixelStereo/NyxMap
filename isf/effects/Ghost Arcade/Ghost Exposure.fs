/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Long-exposure accumulation with configurable decay, hue shift per frame, and add/max/screen modes. Adapted from Ghost Arcade's Ghost Exposure effect.",
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
            "NAME": "exposure",
            "LABEL": "Exposure",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "decay",
            "LABEL": "Decay",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.04
        },
        {
            "NAME": "hueShiftPerFrame",
            "LABEL": "Hue Shift Per Frame",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.005
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
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "clampValue",
            "LABEL": "Clamp",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
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

// Ported from Ghost Arcade (ghostExposureHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uExposure (exposure)
#define uDecay (decay)
#define uHueShiftPerFrame (hueShiftPerFrame)
#define uIntensity (intensity)
#define uMode float(floor(modeValue + 0.5))
#define uClamp (clampValue)
#define uHasFeedback (FRAMEINDEX > 0 ? 1.0 : 0.0)

      // 0-1 how much new frame contributes
         // 0-1 how fast old fades
 // 0-0.05
     // 0-2
          // 0=add, 1=max, 2=screen
         // 0-1 (limit accumulation)

vec3 hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
vec3 rgb2hsv(vec3 c) {
  vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}

void ghostArcadeEffect() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec3 prev = (uHasFeedback > 0.5) ? IMG_NORM_PIXEL(feedbackBuffer, vUv).rgb : vec3(0.0);
  prev *= 1.0 - uDecay;

  // Hue shift per frame
  if (uHueShiftPerFrame > 0.001 && uHasFeedback > 0.5) {
    vec3 hsv = rgb2hsv(prev);
    hsv.x = fract(hsv.x + uHueShiftPerFrame);
    prev = hsv2rgb(hsv);
  }

  int mode = int(uMode + 0.5);
  vec3 result;
  if (mode == 0) result = prev + src * uExposure;
  else if (mode == 1) result = max(prev, src * uExposure);
  else result = 1.0 - (1.0 - prev) * (1.0 - src * uExposure);

  if (uClamp > 0.001) result = clamp(result, vec3(0.0), vec3(uClamp + 1.0 - uClamp));
  result *= uIntensity;
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}

void main() {
    if (PASSINDEX == 0) ghostArcadeEffect();
    else gl_FragColor = IMG_NORM_PIXEL(feedbackBuffer, isf_FragNormCoord);
}
