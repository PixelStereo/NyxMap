/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Organic cell-based fragmentation with drifting edges and depth lighting. Adapted from Ghost Arcade's Voronoi Shatter effect.",
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
            "LABEL": "Cell Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount3",
            "LABEL": "Drift Speed",
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
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (voronoiShatterShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

vec2 hash22(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.xx + p3.yz) * p3.zy);
}

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float blend = uAmount;
  float cellCount = mix(4.0, 24.0, uAmount2);
  float driftSpeed = uAmount3 * 1.5;
  float gap = uThreshold * 0.06;

  vec2 aspect = vec2(uResolution.x / uResolution.y, 1.0);
  vec2 p = uv * aspect * cellCount;

  // Find nearest Voronoi cell
  float minDist = 10.0;
  float secondDist = 10.0;
  vec2 nearestId = vec2(0.0);
  vec2 nearestPoint = vec2(0.0);

  // Check 3x3 neighborhood
  vec2 ip = floor(p);
  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      vec2 neighbor = ip + vec2(float(x), float(y));
      vec2 rnd = hash22(neighbor);

      // Animate cell centers
      vec2 cellPoint = neighbor + 0.5 + 0.35 * sin(uTime * driftSpeed + rnd * TAU);

      float d = length(p - cellPoint);
      if (d < minDist) {
        secondDist = minDist;
        minDist = d;
        nearestId = neighbor;
        nearestPoint = cellPoint;
      } else if (d < secondDist) {
        secondDist = d;
      }
    }
  }

  // Edge detection
  float edgeDist = secondDist - minDist;
  float edge = smoothstep(gap, gap + 0.04, edgeDist);

  // Map cell center to texture UV
  vec2 cellUV = nearestPoint / cellCount / aspect;

  // Per-cell rotation
  vec2 cellRnd = hash22(nearestId);
  float cellAngle = sin(uTime * driftSpeed * 0.7 + cellRnd.x * TAU) * 0.2;
  float cca = cos(cellAngle), csa = sin(cellAngle);

  // Local offset with rotation
  vec2 localP = (p - nearestPoint) / cellCount / aspect;
  vec2 rotLocal = vec2(localP.x * cca - localP.y * csa,
                       localP.x * csa + localP.y * cca);

  vec2 texCoord = clamp(cellUV + rotLocal, 0.0, 1.0);
  vec3 texCol = IMG_NORM_PIXEL(inputImage, texCoord).rgb;

  // Depth lighting
  float depth = cellRnd.y;
  float lighting = 0.6 + depth * 0.4;
  float shadowSide = (nearestPoint.x - p.x) * 0.2;
  lighting += shadowSide;

  vec3 col = texCol * lighting * edge;

  // Edge highlight
  float edgeHighlight = smoothstep(gap + 0.06, gap + 0.01, edgeDist) * edge;
  col += vec3(0.3, 0.35, 0.5) * edgeHighlight * 0.4;

  col = mix(vec3(0.01), col, edge);

  col = mix(src.rgb, col, blend);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
