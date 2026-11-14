/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Animated smoke wisps dissolve the image. Top-down / center-out / fbm-driven directions with wind angle. Adapted from Ghost Arcade's Smoke Disintegrate effect.",
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
            "NAME": "amountValue",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "scaleValue",
            "LABEL": "Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "directionValue",
            "LABEL": "Direction",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 180.0,
            "DEFAULT": 90.0
        },
        {
            "NAME": "edgeFade",
            "LABEL": "Edge Fade",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "smokeColorR",
            "LABEL": "Smoke Color R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
        },
        {
            "NAME": "smokeColorG",
            "LABEL": "Smoke Color G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
        },
        {
            "NAME": "smokeColorB",
            "LABEL": "Smoke Color B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.8,
            "DEFAULT": 0.9
        },
        {
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (smokeDisintegrateHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amountValue)
#define uScale (scaleValue)
#define uSpeed (speed)
#define uDirection (directionValue)
#define uEdgeFade (edgeFade)
#define uSmokeColorR (smokeColorR)
#define uSmokeColorG (smokeColorG)
#define uSmokeColorB (smokeColorB)
#define uMode float(floor(modeValue + 0.5))

        // 0-1 0=intact, 1=fully gone
         // 0.5-16 noise scale
         // 0-3
     // 0-360 wind direction
      // 0-1 fade at dissolve edge



          // 0=top-down, 1=center-out, 2=fbm-driven

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  float a = hash21(i), b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm(vec2 p) {
  float v = 0.0; float amp = 0.5;
  for (int i = 0; i < 5; i++) { v += vnoise(p) * amp; p *= 2.0; amp *= 0.5; }
  return v;
}
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  int mode = int(uMode + 0.5);
  float ang = radians(uDirection);
  vec2 windDir = vec2(cos(ang), sin(ang));

  // Base threshold field (where dissolve has reached)
  float threshold;
  if (mode == 0) threshold = vUv.y; // top-down
  else if (mode == 1) threshold = 1.0 - length(vUv - 0.5) * 1.4; // center-out
  else threshold = fbm(vUv * 2.0); // fbm-driven random

  // Animated smoke noise pattern
  vec2 p = vUv * uScale + windDir * uTime * uSpeed * 0.2;
  float smoke = fbm(p);
  smoke = smoke * 0.6 + fbm(p * 2.5 + uTime * uSpeed * 0.15) * 0.4;

  // Threshold + amount drives dissolve
  float dissolveEdge = uAmount + smoke * 0.5 - 0.5;
  float dissolveMask = smoothstep(threshold - uEdgeFade * 0.2, threshold + uEdgeFade * 0.2, dissolveEdge);

  // Smoke color in dissolve area (tint by smoke pattern)
  vec3 smokeColor = vec3(uSmokeColorR, uSmokeColorG, uSmokeColorB) * (0.6 + smoke * 0.4);
  vec3 result = mix(src, smokeColor, dissolveMask);
  // Apply alpha mask: high dissolve = transparent (or smoke-tinted)
  float alpha = 1.0 - dissolveMask * 0.6;

  gl_FragColor = vec4(result, alpha);
}
