/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Logarithmic spiral sectors with animated spin and flip effects. Adapted from Ghost Arcade's Spiral Tiles effect.",
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
            "LABEL": "Arms / Rings",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount3",
            "LABEL": "Spin Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "threshold",
            "LABEL": "Gap",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "angleValue",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (spiralTileShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(mode + 0.5))
#define uAmount (amount)
#define uAmount2 (amount2)
#define uAmount3 (amount3)
#define uThreshold (threshold)
#define uAngle (angleValue)
#define uCenter vec2(0.5, 0.5)
#define uColor vec3(0.5, 0.5, 0.5)

#define PI 3.14159265359
#define TAU 6.28318530718

float hash11(float p) {
  return fract(sin(p * 127.1) * 43758.5453);
}

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float blend = uAmount;
  float arms = floor(3.0 + uAmount2 * 9.0);
  float spinSpeed = uAmount3 * 2.0;
  float gap = uThreshold * 0.08;

  vec2 centered = (uv - 0.5) * vec2(uResolution.x / uResolution.y, 1.0);
  float r = length(centered);
  float a = atan(centered.y, centered.x);

  // Logarithmic spiral: angle depends on log of radius
  float spiralTwist = 3.0;
  float spiralA = a + log(max(r, 0.001)) * spiralTwist - uTime * spinSpeed;

  // Divide into sectors
  float sectorAngle = TAU / arms;
  float sectorId = floor(spiralA / sectorAngle);
  float sectorFrac = fract(spiralA / sectorAngle);

  // Radial rings
  float ringCount = 6.0 + uAmount2 * 10.0;
  float ringId = floor(r * ringCount);
  float ringFrac = fract(r * ringCount);

  // Combined tile ID
  float tileId = sectorId * 100.0 + ringId;
  float rnd = hash11(tileId);

  // Gaps
  float sectorGap = smoothstep(0.0, gap, sectorFrac) * smoothstep(1.0, 1.0 - gap, sectorFrac);
  float ringGap = smoothstep(0.0, gap * 2.0, ringFrac) * smoothstep(1.0, 1.0 - gap * 2.0, ringFrac);
  float inTile = sectorGap * ringGap;

  // Per-tile animation
  float tilePhase = rnd * TAU;
  float tileFlip = sin(uTime * spinSpeed * 1.5 + tilePhase);

  // Map tile center back to source UV
  float centerA = (sectorId + 0.5) * sectorAngle - log(max(r, 0.001)) * spiralTwist + uTime * spinSpeed;
  float centerR = (ringId + 0.5) / ringCount;
  vec2 tileCenterUV = vec2(cos(centerA), sin(centerA)) * centerR;
  tileCenterUV.x /= uResolution.x / uResolution.y;
  vec2 texCoord = clamp(tileCenterUV + 0.5, 0.0, 1.0);

  vec3 texCol = IMG_NORM_PIXEL(inputImage, texCoord).rgb;

  // Perspective flip effect
  float lighting = 0.5 + 0.5 * abs(cos(tileFlip * PI * 0.5));

  vec3 col = texCol * lighting * inTile;
  col = mix(vec3(0.015), col, inTile);

  // Radial fade at center and edges
  float radialFade = smoothstep(0.0, 0.05, r) * smoothstep(1.2, 0.8, r);
  col *= radialFade;

  col = mix(src.rgb, col, blend);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
