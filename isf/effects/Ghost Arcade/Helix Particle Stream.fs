/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Multiple helical curves rising over time like a DNA double-helix made of source pixels. Adapted from Ghost Arcade's Helix Particle Stream effect.",
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
            "NAME": "helices",
            "LABEL": "Helices",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 4.0,
            "DEFAULT": 2.0
        },
        {
            "NAME": "helixRadius",
            "LABEL": "Helix Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "turnsValue",
            "LABEL": "Turns",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 6.0,
            "DEFAULT": 3.0
        },
        {
            "NAME": "heightValue",
            "LABEL": "Height",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 4.0,
            "DEFAULT": 2.0
        },
        {
            "NAME": "tubeRadius",
            "LABEL": "Tube Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.06
        },
        {
            "NAME": "riseSpeed",
            "LABEL": "Rise Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "spin",
            "LABEL": "Spin",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "tilt",
            "LABEL": "Tilt",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
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

// Ported from Ghost Arcade (helixParticleStreamShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uHelices (helices)
#define uHelixRadius (helixRadius)
#define uTurns (turnsValue)
#define uHeight (heightValue)
#define uTubeRadius (tubeRadius)
#define uRiseSpeed (riseSpeed)
#define uSpin (spin)
#define uTilt (tilt)
#define uIntensity (intensity)
#define uTint vec3(0.4, 1.0, 0.7)
#define uHorizonFade (horizonFade)
#define uAudioBass 0.0
#define uAudioBeatPulse 0.0

       // 1-6
   // 0.2-1
         // 1-6
        // 1-3
    // 0.02-0.15
     // 0-3
          // -3..3
          // 0-1
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


vec3 helixPoint(float helixIdx, float t, float radius, float turns, float height) {
  float ang = t * turns * TAU + helixIdx * (TAU / max(1.0, uHelices));
  return vec3(cos(ang) * radius, (t - 0.5) * height, sin(ang) * radius);
}

void main() {
  vec3 srcRaw = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec2 p = (gl_FragCoord.xy * 2.0 - uResolution.xy) / uResolution.y;
  vec3 ro = vec3(0.0, 0.0, 3.0);
  vec3 rd = normalize(vec3(p, -1.5));

  mat3 rot = rotY(uTime * uSpin * 0.3) * rotX(uTilt * PI * 0.5);
  ro = rot * ro;
  rd = rot * rd;

  int n = int(clamp(uHelices, 1.0, 6.0));
  int steps = 48;
  float bestT = 1e9;
  float bestU = 0.0;
  vec3 bestPt = vec3(0.0);
  float bestHelix = 0.0;
  float r = uTubeRadius * (1.0 + uAudioBeatPulse * 0.15);

  for (int h = 0; h < 6; h++) {
    if (h >= n) break;
    for (int i = 0; i < 48; i++) {
      float ti = float(i) / float(steps - 1);
      // Animated rise: shift t by time
      ti = fract(ti + uTime * uRiseSpeed * 0.05);
      vec3 pt = helixPoint(float(h), ti, uHelixRadius, uTurns, uHeight);
      float t = intersectSphere(ro - pt, rd, r);
      if (t > 0.0 && t < bestT) {
        bestT = t;
        bestU = ti;
        bestPt = pt;
        bestHelix = float(h);
      }
    }
  }

  if (bestT > 1e8) {
    gl_FragColor = vec4(uTint * 0.04, 1.0 - uHorizonFade);
    return;
  }

  vec3 hit = ro + rd * bestT;
  vec3 N = normalize(hit - bestPt);
  vec2 sUv = vec2(bestU, bestHelix / max(1.0, uHelices));
  vec3 tex = IMG_NORM_PIXEL(inputImage, sUv).rgb;

  vec3 lightDir = normalize(vec3(0.5, 0.7, 0.5));
  float diff = max(dot(N, lightDir), 0.0);
  vec3 col = tex * uIntensity * (0.5 + diff * 0.7) + uTint * uAudioBass * 0.3;

  float fade = 1.0 - clamp((bestT - 2.0) / 2.5, 0.0, 1.0);
  float alpha = mix(1.0, fade, uHorizonFade);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), alpha);
}
