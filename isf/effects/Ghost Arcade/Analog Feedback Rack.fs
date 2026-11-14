/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Real feedback buffer with zoom + rotate + decay + hue shift + center mask. Recursive frame accumulation. Adapted from Ghost Arcade's Analog Feedback Rack effect.",
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
            "NAME": "mixValue",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "zoom",
            "LABEL": "Zoom",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.04,
            "DEFAULT": 1.02
        },
        {
            "NAME": "rotation",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.005
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
            "NAME": "hueShift",
            "LABEL": "Hue Shift",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.01
        },
        {
            "NAME": "maskCenter",
            "LABEL": "Mask Center",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "chromaSplit",
            "LABEL": "Chroma Split",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
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
            "NAME": "offsetY",
            "LABEL": "Offset Y",
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

// Ported from Ghost Arcade (analogFeedbackRackHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMix (mixValue)
#define uZoom (zoom)
#define uRotation (rotation)
#define uDecay (decay)
#define uHueShift (hueShift)
#define uMaskCenter (maskCenter)
#define uChromaSplit (chromaSplit)
#define uOffsetX (offsetX)
#define uOffsetY (offsetY)
#define uMode float(floor(modeValue + 0.5))
#define uHasFeedback (FRAMEINDEX > 0 ? 1.0 : 0.0)

           // 0-1 fresh-vs-feedback
          // 0.85-1.15
      // -0.2..0.2 radians per frame
         // 0-1
      // 0-1 per frame
    // 0-1 vignette around center
   // 0-1
       // -0.1..0.1 per frame translate
       // -0.1..0.1
          // 0=normal, 1=invert blend, 2=multiply

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
  if (uHasFeedback < 0.5) { gl_FragColor = vec4(src, 1.0); return; }

  // Sample feedback at zoomed/rotated/translated UV
  vec2 d = (vUv - 0.5) * uZoom;
  float ca = cos(uRotation), sa = sin(uRotation);
  d = vec2(d.x * ca - d.y * sa, d.x * sa + d.y * ca);
  vec2 fbUv = 0.5 + d - vec2(uOffsetX, uOffsetY);

  vec3 fb;
  if (uChromaSplit > 0.001) {
    fb.r = IMG_NORM_PIXEL(feedbackBuffer, fbUv + vec2(uChromaSplit * 0.005, 0)).r;
    fb.g = IMG_NORM_PIXEL(feedbackBuffer, fbUv).g;
    fb.b = IMG_NORM_PIXEL(feedbackBuffer, fbUv - vec2(uChromaSplit * 0.005, 0)).b;
  } else {
    fb = IMG_NORM_PIXEL(feedbackBuffer, fbUv).rgb;
  }
  fb *= 1.0 - uDecay;

  if (uHueShift > 0.001) {
    vec3 hsv = rgb2hsv(fb);
    hsv.x = fract(hsv.x + uHueShift);
    fb = hsv2rgb(hsv);
  }

  // Center mask: only feedback in center, fade edges
  if (uMaskCenter > 0.001) {
    float mask = 1.0 - smoothstep(0.2, 0.8, length(vUv - 0.5));
    fb *= mix(1.0, mask, uMaskCenter);
  }

  int mode = int(uMode + 0.5);
  vec3 result;
  if (mode == 0) result = mix(src, src + fb, uMix);
  else if (mode == 1) result = mix(src, 1.0 - (1.0 - src) * (1.0 - fb), uMix);
  else result = mix(src, src * (1.0 + fb), uMix);

  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}

void main() {
    if (PASSINDEX == 0) ghostArcadeEffect();
    else gl_FragColor = IMG_NORM_PIXEL(feedbackBuffer, isf_FragNormCoord);
}
