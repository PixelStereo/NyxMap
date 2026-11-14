/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Fly through a torus donut hole with texture mapped on the inner surface. Adapted from Ghost Arcade's Torus Tunnel effect.",
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
            "LABEL": "Tube Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount3",
            "LABEL": "Fly Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "threshold",
            "LABEL": "Twist",
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

// Ported from Ghost Arcade (torusTunnelShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

float sdTorus(vec3 p, float R, float r) {
  vec2 q = vec2(length(p.xz) - R, p.y);
  return length(q) - r;
}

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float blend = uAmount;
  float tubeRadius = 0.3 + uAmount2 * 0.7;
  float flySpeed = uAmount3 * 1.5;
  float twist = uThreshold * 4.0;

  vec2 p = (uv - 0.5) * 2.0;
  p.x *= uResolution.x / uResolution.y;

  float majorR = 1.5;
  float t = uTime * flySpeed;

  // Camera position on the ring path
  float camAngle = t;
  vec3 camPos = vec3(cos(camAngle) * majorR, 0.0, sin(camAngle) * majorR);

  // Camera forward tangent to ring
  vec3 camFwd = normalize(vec3(-sin(camAngle), 0.0, cos(camAngle)));
  vec3 camUp = vec3(0.0, 1.0, 0.0);
  vec3 camRight = normalize(cross(camFwd, camUp));
  camUp = cross(camRight, camFwd);

  // Apply twist
  float tw = t * twist;
  vec3 tRight = camRight * cos(tw) + camUp * sin(tw);
  vec3 tUp = -camRight * sin(tw) + camUp * cos(tw);

  vec3 rd = normalize(camFwd + p.x * tRight * 0.8 + p.y * tUp * 0.8);

  // Move camera slightly inside the tube
  camPos += camFwd * 0.01;

  // Raymarch the inner surface of the torus
  vec3 col = vec3(0.0);
  float totalDist = 0.0;
  bool hit = false;

  for (int i = 0; i < 60; i++) {
    vec3 pos = camPos + rd * totalDist;
    float d = -sdTorus(pos, majorR, tubeRadius);
    if (d < 0.002) {
      hit = true;
      break;
    }
    if (totalDist > 10.0) break;
    totalDist += max(d, 0.005);
  }

  if (hit) {
    vec3 hitPos = camPos + rd * totalDist;

    // Map hit to texture coordinates
    float ringAngle = atan(hitPos.z, hitPos.x);
    vec2 localP = vec2(length(hitPos.xz) - majorR, hitPos.y);
    float tubeAngle = atan(localP.y, localP.x);

    vec2 texUV = vec2(
      fract(ringAngle / TAU + 0.5),
      fract(tubeAngle / TAU + 0.5)
    );

    vec3 texCol = IMG_NORM_PIXEL(inputImage, texUV).rgb;

    // Simple depth fog
    float fog = exp(-totalDist * 0.3);
    col = texCol * fog;

    // Subtle ambient
    col += vec3(0.02, 0.01, 0.03);
  }

  col = mix(src.rgb, col, blend);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
