/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Target and shift specific hue ranges. Adapted from Ghost Arcade's Selective Color effect.",
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
            "NAME": "targetHue",
            "LABEL": "Target Hue",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "hueRange",
            "LABEL": "Hue Range",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
        },
        {
            "NAME": "feather",
            "LABEL": "Feather",
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
            "NAME": "replaceHue",
            "LABEL": "Replace Hue",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.33
        },
        {
            "NAME": "satBoost",
            "LABEL": "Sat Boost",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (selectiveColorHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uTargetHue (targetHue)
#define uHueRange (hueRange)
#define uFeather (feather)
#define uMode float(floor(modeValue + 0.5))
#define uReplaceHue (replaceHue)
#define uSatBoost (satBoost)

    // 0-1 — hue to target (0=red, 0.33=green, 0.67=blue)
     // 0-1 — width of the hue band (0.05 = narrow, 0.3 = wide)
      // 0-1 — soft falloff at the edge of the band
         // 0=isolate (desat outside), 1=replace (hue shift target)
   // 0-1 — destination hue for replace mode
     // 0-1 — saturation boost on targeted pixels (for color pop)

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

  // Hue distance with circular wrap.
  float d = abs(hsv.x - uTargetHue);
  d = min(d, 1.0 - d);

  // Smooth falloff: 1.0 inside the band, 0.0 outside, soft at edges.
  float band = 1.0 - smoothstep(uHueRange, uHueRange + uFeather, d);

  int mode = int(uMode + 0.5);
  if (mode == 0) {
    // Isolate — desaturate everything OUTSIDE the band.
    hsv.y *= mix(0.0, 1.0, band);
    // Optional sat boost on pixels INSIDE the band.
    hsv.y = clamp(hsv.y + band * uSatBoost * 0.5, 0.0, 1.0);
  } else {
    // Replace — shift hue toward uReplaceHue for pixels INSIDE the band.
    float hueDelta = uReplaceHue - uTargetHue;
    // Take the shortest way around the hue circle.
    if (hueDelta > 0.5) hueDelta -= 1.0;
    if (hueDelta < -0.5) hueDelta += 1.0;
    hsv.x = fract(hsv.x + hueDelta * band);
    hsv.y = clamp(hsv.y + band * uSatBoost * 0.5, 0.0, 1.0);
  }

  gl_FragColor = vec4(hsv2rgb(hsv), texColor.a);
}
