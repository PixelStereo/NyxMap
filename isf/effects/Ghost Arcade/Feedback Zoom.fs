/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Simulated video feedback — iterative zoom + rotate creates hypnotic spiral trails. Adapted from Ghost Arcade's Feedback Zoom effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Feedback",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "amount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "zoom",
            "LABEL": "Zoom",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.08,
            "DEFAULT": 1.04
        },
        {
            "NAME": "rotation",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.02
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
            "DEFAULT": 0.0
        },
        {
            "NAME": "maskCenter",
            "LABEL": "Mask Center",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
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

// Ported from Ghost Arcade (feedbackZoomHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uZoom (zoom)
#define uRotation (rotation)
#define uDecay (decay)
#define uHueShift (hueShift)
#define uMaskCenter (maskCenter)
#define uHasFeedback (FRAMEINDEX > 0 ? 1.0 : 0.0)

vec3 rgb2hsv(vec3 c) {
  vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10;
  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
vec3 hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void ghostArcadeEffect() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uHasFeedback < 0.5) { gl_FragColor = src; return; }

  vec2 centered = vUv - 0.5;
  float c = cos(uRotation);
  float s = sin(uRotation);
  centered = mat2(c, -s, s, c) * centered;
  centered /= max(uZoom, 0.001);
  vec2 fbUv = centered + 0.5;
  vec3 fb = IMG_NORM_PIXEL(feedbackBuffer, fbUv).rgb;

  float keepFactor = clamp(1.0 - uDecay, 0.0, 1.0);
  fb *= keepFactor;

  if (uHueShift > 0.001) {
    vec3 hsv = rgb2hsv(fb);
    hsv.x = fract(hsv.x + uHueShift * 0.1);
    fb = hsv2rgb(hsv);
  }

  float maskFactor = 1.0;
  if (uMaskCenter > 0.001) {
    float distFromCenter = length(vUv - 0.5);
    maskFactor = 1.0 - smoothstep(0.3, 0.7, distFromCenter * uMaskCenter);
  }
  vec3 fbBlended = fb * maskFactor;
  vec3 composited = 1.0 - (1.0 - src.rgb) * (1.0 - fbBlended);
  vec3 finalColor = mix(src.rgb, composited, uAmount);
  gl_FragColor = vec4(finalColor, max(src.a, fbBlended.r * uAmount));
}

void main() {
    if (PASSINDEX == 0) ghostArcadeEffect();
    else gl_FragColor = IMG_NORM_PIXEL(feedbackBuffer, isf_FragNormCoord);
}
