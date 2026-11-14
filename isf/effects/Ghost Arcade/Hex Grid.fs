/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Honeycomb hexagonal tiles that pop and rotate with animated depth lighting. Adapted from Ghost Arcade's Hex Grid effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced 3D",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "mode",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "amount",
            "LABEL": "Blend",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.8
        },
        {
            "NAME": "amount2",
            "LABEL": "Hex Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount3",
            "LABEL": "Pop Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "threshold",
            "LABEL": "Gap",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (hexGridShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(mode + 0.5))
#define uAmount (amount)
#define uAmount2 (amount2)
#define uAmount3 (amount3)
#define uThreshold (threshold)
#define uAngle (angle)
#define uCenter vec2(0.5, 0.5)
#define uColor vec3(0.5, 0.5, 0.5)

#define PI 3.14159265359
#define TAU 6.28318530718
#define SQRT3 1.7320508

float hash21(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float blend = uAmount;
  float tileSize = mix(4.0, 20.0, uAmount2);
  float popSpeed = uAmount3 * 3.0;
  float gapSize = uThreshold * 0.15;

  // Hex grid math
  vec2 aspect = vec2(uResolution.x / uResolution.y, 1.0);
  vec2 p = uv * aspect * tileSize;

  // Two candidate hex centers (offset rows)
  vec2 a = mod(p, vec2(1.0, SQRT3)) - vec2(0.5, SQRT3 * 0.5);
  vec2 b = mod(p - vec2(0.5, SQRT3 * 0.5), vec2(1.0, SQRT3)) - vec2(0.5, SQRT3 * 0.5);

  // Pick the closer hex center
  vec2 gv = dot(a, a) < dot(b, b) ? a : b;
  vec2 hexId = floor(p - gv + 0.5);

  // Distance to hex edge (approximate with max of 3 axis distances)
  float dx = abs(gv.x);
  float dy = abs(gv.y);
  float hexDist = max(dx, (dx * 0.5 + dy * SQRT3 * 0.5));
  float hexEdge = 0.5 - gapSize;
  float inHex = smoothstep(hexEdge + 0.01, hexEdge - 0.01, hexDist);

  // Per-hex animation
  float rnd = hash21(hexId);
  float phase = rnd * TAU;
  float pop = sin(uTime * popSpeed + phase) * 0.5 + 0.5;

  // Hex-local UV for texture sampling
  vec2 hexUV = (hexId / tileSize) / aspect;
  vec2 localOffset = gv / tileSize / aspect;

  // Slight rotation per hex
  float rotA = sin(uTime * popSpeed * 0.5 + phase) * 0.3 * uAmount3;
  float cr = cos(rotA), sr = sin(rotA);
  vec2 rotOffset = vec2(localOffset.x * cr - localOffset.y * sr,
                        localOffset.x * sr + localOffset.y * cr);

  vec2 texCoord = clamp(hexUV + rotOffset, 0.0, 1.0);
  vec3 texCol = IMG_NORM_PIXEL(inputImage, texCoord).rgb;

  // Lighting: raised hexes are brighter
  float lighting = 0.6 + pop * 0.5;

  // Edge highlight
  float edgeGlow = smoothstep(hexEdge, hexEdge - 0.06, hexDist) -
                   smoothstep(hexEdge - 0.06, hexEdge - 0.12, hexDist);

  vec3 col = texCol * lighting * inHex;
  col += vec3(0.4, 0.5, 0.7) * edgeGlow * 0.3 * pop;

  // Dark background in gaps
  col = mix(vec3(0.01), col, inHex);

  col = mix(src.rgb, col, blend);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
