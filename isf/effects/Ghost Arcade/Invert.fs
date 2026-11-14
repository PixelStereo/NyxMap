/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Invert all colors. Adapted from Ghost Arcade's Invert effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Color",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
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
            "NAME": "amountValue",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "thresholdValue",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "strobeRate",
            "LABEL": "Strobe Rate",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        }
    ]
}*/

// Ported from Ghost Arcade (invertShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(modeValue + 0.5))
#define uAmount (amountValue)
#define uThreshold (thresholdValue)
#define uStrobeRate (strobeRate)

        // 0=RGB,1=luma-only,2=hue,3=strobe,4=threshold-above
      // 0-1 invert strength (partial invert at <1)
   // 0-1 — used in threshold mode (invert pixels brighter than this)
  // 0-10 — strobe Hz when uMode=3

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

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 src = texColor.rgb;
  int mode = int(uMode + 0.5);

  vec3 inverted = src;

  if (mode == 0) {
    // RGB invert (legacy at amount=1)
    inverted = 1.0 - src;
  } else if (mode == 1) {
    // Luma-only — invert brightness, keep hue/saturation. Useful for
    // "x-ray" look where colors stay but light/dark flip.
    vec3 hsv = rgb2hsv(src);
    hsv.z = 1.0 - hsv.z;
    inverted = hsv2rgb(hsv);
  } else if (mode == 2) {
    // Hue invert — rotate hue 180° (complement colors). Keeps
    // brightness/saturation; red→cyan, green→magenta, blue→yellow.
    vec3 hsv = rgb2hsv(src);
    hsv.x = fract(hsv.x + 0.5);
    inverted = hsv2rgb(hsv);
  } else if (mode == 3) {
    // Strobe invert — alternates between original and inverted at
    // uStrobeRate Hz. Phase = floor(time * rate) % 2.
    float phase = mod(floor(uTime * max(uStrobeRate, 0.01)), 2.0);
    inverted = mix(src, 1.0 - src, phase);
  } else {
    // Threshold-above — invert pixels brighter than uThreshold,
    // leave shadows untouched. Great for crushing highlights.
    float lum = dot(src, vec3(0.299, 0.587, 0.114));
    float invertMask = smoothstep(uThreshold - 0.02, uThreshold + 0.02, lum);
    inverted = mix(src, 1.0 - src, invertMask);
  }

  // Partial-invert: blend toward the inverted result by uAmount.
  vec3 finalColor = mix(src, inverted, uAmount);

  gl_FragColor = vec4(finalColor, texColor.a);
}
