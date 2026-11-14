/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Paper-rip mask reveals duplicated/offset layers beneath. Line / arc / rectangle tear shapes with edge glow. Adapted from Ghost Arcade's Poster Tear effect.",
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
            "NAME": "tearAmount",
            "LABEL": "Tear Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "tearAngle",
            "LABEL": "Tear Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 70.0,
            "DEFAULT": 35.0
        },
        {
            "NAME": "tearJitter",
            "LABEL": "Tear Jitter",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "shiftBelow",
            "LABEL": "Shift Below",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "offsetX",
            "LABEL": "Offset X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.05
        },
        {
            "NAME": "offsetY",
            "LABEL": "Offset Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.02
        },
        {
            "NAME": "tearGlow",
            "LABEL": "Tear Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
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

// Ported from Ghost Arcade (posterTearHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uTearAmount (tearAmount)
#define uTearAngle (tearAngle)
#define uTearJitter (tearJitter)
#define uShiftBelow (shiftBelow)
#define uOffsetX (offsetX)
#define uOffsetY (offsetY)
#define uTearGlow (tearGlow)
#define uMode float(floor(modeValue + 0.5))

    // 0-1 progress
     // 0-360
    // 0-1 ragged-edge noise
    // 0-1 offset of underneath layer
       // -0.3..0.3
       // -0.3..0.3
      // 0-1 highlight along rip
          // 0=line, 1=arc, 2=rectangle

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  float a = hash21(i), b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

void main() {
  int mode = int(uMode + 0.5);
  float ang = radians(uTearAngle);
  vec2 dir = vec2(cos(ang), sin(ang));
  vec2 norm = vec2(-dir.y, dir.x);

  // Distance from tear path
  vec2 d = vUv - 0.5;
  float distToLine = dot(d, norm);
  float jit = uTearJitter * (vnoise(vUv * 30.0) - 0.5) * 0.05;
  float teared;
  if (mode == 0) {
    teared = step(uTearAmount * 1.0 - 0.5, distToLine + jit);
  } else if (mode == 1) {
    // Arc tear
    float r = length(d);
    teared = step(uTearAmount * 0.7, r + jit);
  } else {
    // Rectangle (corner tear)
    vec2 ad = abs(d);
    teared = step(uTearAmount * 0.5, max(ad.x, ad.y) + jit);
  }

  // Below layer = offset & faded source
  vec2 belowUv = vUv + vec2(uOffsetX, uOffsetY) * uShiftBelow;
  belowUv = clamp(belowUv, vec2(0.0), vec2(1.0));
  vec3 above = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec3 below = IMG_NORM_PIXEL(inputImage, belowUv).rgb * 0.7;

  vec3 result = mix(above, below, 1.0 - teared);

  // Glow along tear edge
  if (uTearGlow > 0.001) {
    float edge = smoothstep(0.04, 0.0, abs(distToLine - (uTearAmount - 0.5)));
    result += vec3(1.0, 0.95, 0.7) * edge * uTearGlow;
  }

  gl_FragColor = vec4(result, 1.0);
}
