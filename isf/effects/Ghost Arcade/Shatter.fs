/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Image fractures into triangular shards that explode outward and reassemble. Adapted from Ghost Arcade's Shatter effect.",
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
            "LABEL": "Explosion",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Shard Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount3",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "threshold",
            "LABEL": "Gravity",
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

// Ported from Ghost Arcade (shatter3DShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

vec2 hash2(vec2 p) {
  p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
  return fract(sin(p) * 43758.5453);
}

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float force = uAmount * 2.0;
  float shardSize = mix(4.0, 20.0, uAmount2);
  float speed = 0.5 + uAmount3 * 2.0;
  float gravity = uThreshold * 0.5;

  // Oscillating explosion factor (0 = assembled, 1 = exploded)
  float phase = sin(uTime * speed) * 0.5 + 0.5;
  float explode = phase * force;

  // Voronoi-based shard pattern
  vec2 cellSize = vec2(shardSize) / uResolution;
  vec2 cellId = floor(uv / cellSize);
  vec2 cellUv = fract(uv / cellSize);

  // Find closest Voronoi center for shard identity
  float minDist = 10.0;
  vec2 closestId = cellId;
  vec2 closestCenter = vec2(0.5);

  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      vec2 neighbor = cellId + vec2(float(x), float(y));
      vec2 point = hash2(neighbor);
      vec2 diff = neighbor + point - uv / cellSize;
      float d = dot(diff, diff);
      if (d < minDist) {
        minDist = d;
        closestId = neighbor;
        closestCenter = neighbor + point;
      }
    }
  }

  // Shard properties based on ID
  vec2 shardHash = hash2(closestId);
  float shardAngle = shardHash.x * TAU;
  float shardDelay = shardHash.y * 0.5;

  // Offset for this shard
  float localExplode = max(explode - shardDelay, 0.0);
  vec2 dir = normalize(closestCenter * cellSize - vec2(0.5));
  vec2 offset = dir * localExplode * 0.3;
  offset.y -= gravity * localExplode * localExplode; // gravity pull

  // Rotation per shard
  float rot = localExplode * shardAngle * 2.0;
  vec2 center = closestCenter * cellSize;
  vec2 rotUv = uv - center;
  float cs = cos(rot), sn = sin(rot);
  rotUv = vec2(rotUv.x * cs - rotUv.y * sn, rotUv.x * sn + rotUv.y * cs);
  rotUv += center;

  vec2 sampleUv = rotUv - offset;

  // Edge detection for shard borders
  float edge = smoothstep(0.01, 0.03, sqrt(minDist));

  // Depth shading based on explode
  float depth = 1.0 - localExplode * 0.3;

  vec3 col = IMG_NORM_PIXEL(inputImage, clamp(sampleUv, 0.0, 1.0)).rgb;
  col *= depth;
  col *= edge; // Darken edges

  // Shard edge highlight
  float edgeHighlight = 1.0 - smoothstep(0.02, 0.06, sqrt(minDist));
  col += edgeHighlight * vec3(0.3) * localExplode;

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
