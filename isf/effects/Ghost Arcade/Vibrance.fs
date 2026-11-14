/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Boost muted colors without clipping saturated ones. Adapted from Ghost Arcade's Vibrance effect.",
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
            "NAME": "vibranceValue",
            "LABEL": "Vibrance",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "skinProtectValue",
            "LABEL": "Skin Protect",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "highlightProtect",
            "LABEL": "Highlight Protect",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "ceiling",
            "LABEL": "Ceiling",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (vibranceHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uVibrance (vibranceValue)
#define uSkinProtect (skinProtectValue)
#define uHighlightProtect (highlightProtect)
#define uCeiling (ceiling)

        // -1..+1 — positive boosts muted colors, negative desaturates
     // 0-1 — reduce vibrance push on skin-tone hues
// 0-1 — reduce vibrance push on bright pixels
         // 0-1 — clamp final saturation (1 = no clamp)

vec3 rgb2hsv(vec3 c) {
  vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}
vec3 hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 hsv = rgb2hsv(texColor.rgb);

  // Skin tone protect — peak around hue ~0.05 (orange-pink ~30°),
  // gaussian falloff. Reduces the vibrance push by skinProtect there.
  float skinDist = abs(hsv.x - 0.05);
  if (skinDist > 0.5) skinDist = 1.0 - skinDist; // hue is circular
  float skinMask = exp(-(skinDist * skinDist) / 0.01); // ~σ=0.1
  float skinScale = 1.0 - uSkinProtect * skinMask;

  // Highlight protect — reduce push on already bright pixels.
  float hlScale = 1.0 - uHighlightProtect * smoothstep(0.6, 1.0, hsv.z);

  // Vibrance boost — non-linear so muted colors lift more than already
  // saturated ones (the classic Lightroom Vibrance behavior).
  float boost = uVibrance * (1.0 - hsv.y) * skinScale * hlScale;
  hsv.y = clamp(hsv.y + boost, 0.0, uCeiling);

  gl_FragColor = vec4(hsv2rgb(hsv), texColor.a);
}
