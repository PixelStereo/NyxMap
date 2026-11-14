/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Texture mapped onto a twisted Möbius band with configurable twist count. Adapted from Ghost Arcade's Möbius Strip effect.",
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
            "LABEL": "Strip Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount3",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "threshold",
            "LABEL": "Twist",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
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

// Ported from Ghost Arcade (mobiusStripShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float blend = uAmount;
  float stripW = 0.1 + uAmount2 * 0.5;
  float rotSpeed = uAmount3 * 1.5;
  float twistAmt = 1.0 + uThreshold * 4.0;

  vec2 p = (uv - 0.5) * 2.0;
  p.x *= uResolution.x / uResolution.y;

  float t = uTime * rotSpeed;

  // Camera setup
  vec3 ro = vec3(0.0, 1.2, -3.0);
  vec3 target = vec3(0.0);
  vec3 fwd = normalize(target - ro);
  vec3 right = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));
  vec3 up = cross(right, fwd);
  vec3 rd = normalize(fwd + p.x * right + p.y * up);

  // Brute-force closest point on Mobius strip
  float minDist = 100.0;
  float bestU = 0.0;
  float bestV = 0.0;

  for (int i = 0; i < 64; i++) {
    float u = float(i) / 64.0 * TAU;
    for (int j = 0; j < 8; j++) {
      float v = float(j) / 7.0 * 2.0 - 1.0;

      float halfTwist = u * twistAmt * 0.5;
      float R = 1.2;
      vec3 mp = vec3(
        (R + v * stripW * cos(halfTwist)) * cos(u + t),
        v * stripW * sin(halfTwist),
        (R + v * stripW * cos(halfTwist)) * sin(u + t)
      );

      // Project onto ray and check distance
      vec3 diff = mp - ro;
      float along = dot(diff, rd);
      if (along > 0.0) {
        vec3 closest = ro + rd * along;
        float d = length(mp - closest);
        if (d < minDist) {
          minDist = d;
          bestU = u;
          bestV = v;
        }
      }
    }
  }

  vec3 col = src.rgb * 0.08;

  if (minDist < 0.08) {
    vec2 texUV = vec2(fract(bestU / TAU), bestV * 0.5 + 0.5);
    vec3 texCol = IMG_NORM_PIXEL(inputImage, texUV).rgb;

    // Shading based on proximity
    float shade = smoothstep(0.08, 0.01, minDist);

    // Simple lighting approximation
    float light = 0.5 + 0.5 * sin(bestU * 2.0 + t);
    texCol *= (0.6 + 0.4 * light);

    col = texCol * shade;
  }

  col = mix(src.rgb, col, blend);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
