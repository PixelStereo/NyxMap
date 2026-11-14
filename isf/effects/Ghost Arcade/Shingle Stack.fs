/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Overlapping brick-pattern tiles with parallax depth like roof shingles. Adapted from Ghost Arcade's Shingle Stack effect.",
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
            "LABEL": "Tile Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount3",
            "LABEL": "Slide Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.25
        },
        {
            "NAME": "threshold",
            "LABEL": "Overlap",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
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

// Ported from Ghost Arcade (shingleStackShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

float hash21(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float blend = uAmount;
  float tileCount = floor(mix(3.0, 14.0, uAmount2));
  float slideSpeed = uAmount3 * 2.0;
  float overlap = 0.2 + uThreshold * 0.4;

  // Grid with row offsets (brick pattern)
  vec2 scaled = uv * tileCount;
  float row = floor(scaled.y);
  float rowOffset = mod(row, 2.0) * 0.5;
  scaled.x += rowOffset;

  vec2 tileId = floor(scaled);
  vec2 tileFrac = fract(scaled);

  // Per-tile random values
  float rnd = hash21(tileId);
  float rnd2 = hash21(tileId + 100.0);

  // Animated shingle lift
  float liftPhase = rnd * TAU + uTime * slideSpeed;
  float lift = sin(liftPhase) * 0.5 + 0.5;

  // Parallax offset
  float parallaxAmount = lift * overlap * 0.3;
  vec2 parallaxOffset = vec2(
    (rnd2 - 0.5) * parallaxAmount,
    -parallaxAmount * 0.5
  );

  // Tile source UV
  vec2 tileUV = (tileId - vec2(rowOffset, 0.0) + tileFrac) / tileCount;
  vec2 texCoord = clamp(tileUV + parallaxOffset, 0.0, 1.0);
  vec3 texCol = IMG_NORM_PIXEL(inputImage, texCoord).rgb;

  // Shadows
  float shadowFromAbove = smoothstep(overlap, 0.0, tileFrac.y) * 0.4;
  float shadowFromSide = smoothstep(overlap * 0.5, 0.0, tileFrac.x) * 0.2;

  // Lighting based on lift
  float lighting = 0.5 + lift * 0.5;

  // Edge bevel
  float edgeX = min(tileFrac.x, 1.0 - tileFrac.x);
  float edgeY = min(tileFrac.y, 1.0 - tileFrac.y);
  float bevel = smoothstep(0.0, 0.08, min(edgeX, edgeY));
  float edgeHighlight = (1.0 - bevel) * lift * 0.3;

  vec3 col = texCol * lighting;
  col *= (1.0 - shadowFromAbove - shadowFromSide);
  col += vec3(edgeHighlight);
  col *= bevel * 0.3 + 0.7;

  col = mix(src.rgb, col, blend);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
