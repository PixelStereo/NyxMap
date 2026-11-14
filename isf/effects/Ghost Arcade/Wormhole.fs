/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Gravitational lensing around a singularity with Einstein ring and accretion disk. Adapted from Ghost Arcade's Wormhole effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Warp",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "pullStrength",
            "LABEL": "Pull Strength",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "rotationValue",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "centerX",
            "LABEL": "Center X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "centerY",
            "LABEL": "Center Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "twist",
            "LABEL": "Twist",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "chromatic",
            "LABEL": "Chromatic",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "animSpeed",
            "LABEL": "Anim Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        }
    ]
}*/

// Ported from Ghost Arcade (wormholeHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uPullStrength (pullStrength)
#define uRotation (rotationValue)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uTwist (twist)
#define uChromatic (chromatic)
#define uAnimSpeed (animSpeed)

  // 0-1
      // 0-3 rotation per radius


         // 0-3
     // 0-1

     // 0-2

vec2 wormholeMap(vec2 uv, float chromaShift) {
  vec2 c = vec2(uCenterX, uCenterY);
  vec2 d = uv - c;
  d.x *= uResolution.x / uResolution.y;
  float r = length(d);
  float ang = atan(d.y, d.x);
  // Twist proportional to inverse radius
  ang += uTwist / max(0.05, r) + uTime * uAnimSpeed * 0.3 + chromaShift;
  // Pull toward center
  r *= mix(1.0, 0.5 + 0.5 * (r * r), uPullStrength);
  d = vec2(cos(ang), sin(ang)) * r;
  d.x *= uResolution.y / uResolution.x;
  return c + d;
}

void main() {
  vec3 col;
  vec2 baseUv = wormholeMap(vUv, 0.0);
  if (uChromatic > 0.001) {
    vec2 uvR = wormholeMap(vUv, uChromatic * 0.1);
    vec2 uvB = wormholeMap(vUv, -uChromatic * 0.1);
    col.r = IMG_NORM_PIXEL(inputImage, clamp(uvR, vec2(0.0), vec2(1.0))).r;
    col.g = IMG_NORM_PIXEL(inputImage, clamp(baseUv, vec2(0.0), vec2(1.0))).g;
    col.b = IMG_NORM_PIXEL(inputImage, clamp(uvB, vec2(0.0), vec2(1.0))).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, clamp(baseUv, vec2(0.0), vec2(1.0))).rgb;
  }
  gl_FragColor = vec4(col, 1.0);
}
