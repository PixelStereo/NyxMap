/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Cylinder grid with source mapped to barrel UV. Audio bass thickens tubes; spiral twist over time. Adapted from Ghost Arcade's Tube Lattice effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Geometric 3D",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "tubeCount",
            "LABEL": "Tube Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 12.0,
            "DEFAULT": 6.0
        },
        {
            "NAME": "tubeRadius",
            "LABEL": "Tube Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.15
        },
        {
            "NAME": "spread",
            "LABEL": "Spread",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.8,
            "DEFAULT": 0.9
        },
        {
            "NAME": "spin",
            "LABEL": "Spin",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "tilt",
            "LABEL": "Tilt",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "twistValue",
            "LABEL": "Twist",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "horizonFade",
            "LABEL": "Horizon Fade",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        }
    ]
}*/

// Ported from Ghost Arcade (tubeLatticeShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uTubeCount (tubeCount)
#define uTubeRadius (tubeRadius)
#define uSpread (spread)
#define uSpin (spin)
#define uTilt (tilt)
#define uTwist (twistValue)
#define uIntensity (intensity)
#define uRimColor vec3(0.0, 0.95, 1.0)
#define uHorizonFade (horizonFade)
#define uAudioBass 0.0
#define uAudioBeatPulse 0.0

     // 3-12
    // 0.05-0.3
        // 0.4-2
          // -3..3
          // 0-1
         // 0-3 spiral twist of tubes
     // 0-2


#define PI 3.14159265359
#define TAU 6.28318530718

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

mat3 rotX(float a) {
  float s = sin(a), c = cos(a);
  return mat3(1.0, 0.0, 0.0,  0.0, c, -s,  0.0, s, c);
}
mat3 rotY(float a) {
  float s = sin(a), c = cos(a);
  return mat3(c, 0.0, s,  0.0, 1.0, 0.0,  -s, 0.0, c);
}
mat3 rotZ(float a) {
  float s = sin(a), c = cos(a);
  return mat3(c, -s, 0.0,  s, c, 0.0,  0.0, 0.0, 1.0);
}

// Map a unit-vector normal to spherical UV (equirectangular).
vec2 sphereUV(vec3 n) {
  return vec2(atan(n.z, n.x) / TAU + 0.5,
              asin(clamp(n.y, -1.0, 1.0)) / PI + 0.5);
}

float fresnel(vec3 n, vec3 rd, float power) {
  return pow(1.0 - max(dot(n, -rd), 0.0), power);
}

// Ray-sphere intersection. Returns t (negative if miss).
float intersectSphere(vec3 ro, vec3 rd, float r) {
  float b = dot(ro, rd);
  float c = dot(ro, ro) - r * r;
  float h = b * b - c;
  if (h < 0.0) return -1.0;
  return -b - sqrt(h);
}

// Ray-axis-aligned-box intersection. Sets outNormal to face normal.
float intersectBox(vec3 ro, vec3 rd, vec3 r, out vec3 outNormal) {
  vec3 m = 1.0 / rd;
  vec3 n = m * ro;
  vec3 k = abs(m) * r;
  vec3 t1 = -n - k;
  vec3 t2 = -n + k;
  float tN = max(max(t1.x, t1.y), t1.z);
  float tF = min(min(t2.x, t2.y), t2.z);
  if (tN > tF || tF < 0.0) return -1.0;
  outNormal = -sign(rd) * step(t1.yzx, t1.xyz) * step(t1.zxy, t1.xyz);
  return tN;
}

// Ray-cylinder (infinite, axis-aligned along Y). Returns nearest positive t.
float intersectCylinderY(vec3 ro, vec3 rd, float r, out vec3 outNormal) {
  float a = rd.x * rd.x + rd.z * rd.z;
  float b = ro.x * rd.x + ro.z * rd.z;
  float c = ro.x * ro.x + ro.z * ro.z - r * r;
  float h = b * b - a * c;
  if (h < 0.0) return -1.0;
  float t = (-b - sqrt(h)) / a;
  vec3 hit = ro + rd * t;
  outNormal = normalize(vec3(hit.x, 0.0, hit.z));
  return t;
}

// Ray-torus intersection (analytic via quartic - approximated with raymarch).
// Returns t when ray is within eps of the torus surface; -1 if miss.
float intersectTorus(vec3 ro, vec3 rd, float majorR, float minorR, out vec3 outNormal) {
  float t = 0.0;
  float maxT = 8.0;
  for (int i = 0; i < 64; i++) {
    vec3 p = ro + rd * t;
    vec2 q = vec2(length(p.xz) - majorR, p.y);
    float d = length(q) - minorR;
    if (d < 0.001) {
      // Approximate normal
      vec2 cq = vec2(length(p.xz) - majorR, p.y);
      vec3 n = vec3(p.x, 0.0, p.z) / max(0.001, length(p.xz));
      outNormal = normalize(vec3(n.x * cq.x, cq.y, n.z * cq.x));
      return t;
    }
    t += max(d, 0.005);
    if (t > maxT) return -1.0;
  }
  return -1.0;
}

float hash13(vec3 p) {
  p = fract(p * vec3(443.8975, 397.2973, 491.1871));
  p += dot(p, p.yzx + 19.19);
  return fract((p.x + p.y) * p.z);
}

vec3 hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}


void main() {
  vec2 p = (gl_FragCoord.xy * 2.0 - uResolution.xy) / uResolution.y;
  vec3 ro = vec3(0.0, 0.0, 3.0);
  vec3 rd = normalize(vec3(p, -1.5));

  mat3 rot = rotY(uTime * uSpin * 0.3) * rotX(uTilt * PI * 0.5);
  ro = rot * ro;
  rd = rot * rd;

  int n = int(clamp(uTubeCount, 1.0, 12.0));
  float bestT = 1e9;
  vec3 bestN = vec3(0.0);
  float bestSeed = 0.0;
  vec3 bestHit = vec3(0.0);

  float bassBoost = 1.0 + uAudioBass * 0.4;
  for (int i = 0; i < 12; i++) {
    if (i >= n) break;
    float fi = float(i);
    float ang = fi / float(n) * TAU;
    vec3 offset = vec3(cos(ang), 0.0, sin(ang)) * uSpread;
    // Twist the tube position
    mat3 twistRot = rotZ(uTwist * sin(uTime * 0.2 + fi));
    offset = twistRot * offset;

    vec3 N;
    float ti = intersectCylinderY(ro - offset, rd, uTubeRadius * bassBoost, N);
    if (ti > 0.0 && ti < bestT) {
      // Validate that hit y is within range (clip to ±1)
      vec3 hit = ro + rd * ti;
      if (abs(hit.y - offset.y) > 1.5) continue;
      bestT = ti;
      bestN = N;
      bestSeed = fi;
      bestHit = hit;
    }
  }

  if (bestT > 1e8) {
    gl_FragColor = vec4(uRimColor * 0.04, 1.0 - uHorizonFade);
    return;
  }

  // Map hit on tube to barrel UV
  float ang = atan(bestN.z, bestN.x) / TAU + 0.5;
  float yU = bestHit.y * 0.5 + 0.5;
  vec2 sUv = vec2(ang, yU);
  vec3 tex = IMG_NORM_PIXEL(inputImage, sUv).rgb;

  float rim = fresnel(bestN, rd, 1.6);
  vec3 col = tex * uIntensity + uRimColor * rim * 0.7 + uRimColor * uAudioBass * 0.2;
  float fade = 1.0 - clamp((bestT - 2.0) / 3.0, 0.0, 1.0);
  float alpha = mix(1.0, fade, uHorizonFade);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), alpha);
}
