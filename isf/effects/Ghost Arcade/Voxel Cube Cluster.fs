/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Floating array of cubes (up to 8×8×8) with per-cube luma extrusion. Each face shows a source patch. Adapted from Ghost Arcade's Voxel Cube Cluster effect.",
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
            "NAME": "gridSize",
            "LABEL": "Grid Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "cubeSize",
            "LABEL": "Cube Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.22
        },
        {
            "NAME": "spacing",
            "LABEL": "Spacing",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "height",
            "LABEL": "Height",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "spin",
            "LABEL": "Spin",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "tilt",
            "LABEL": "Tilt",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "camDistance",
            "LABEL": "Cam Distance",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "specular",
            "LABEL": "Specular",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "ambient",
            "LABEL": "Ambient",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
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

// Ported from Ghost Arcade (voxelCubeClusterShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uGridSize (gridSize)
#define uCubeSize (cubeSize)
#define uSpacing (spacing)
#define uHeight (height)
#define uSpin (spin)
#define uTilt (tilt)
#define uCamDistance (camDistance)
#define uSpecular (specular)
#define uAmbient (ambient)
#define uHorizonFade (horizonFade)
#define uBgColor vec3(0.04, 0.05, 0.08)
#define uAudioBass 0.0
#define uAudioBeatPulse 0.0

      // 2-8
      // 0.1-0.45
       // 0.4-1.2
        // 0-1 luma extrusion
          // 0-3
          // 0-1
   // 2-6
      // 0-1
       // 0-1


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
  vec3 srcRaw = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec2 p = (gl_FragCoord.xy * 2.0 - uResolution.xy) / uResolution.y;
  vec3 ro = vec3(0.0, 0.0, uCamDistance);
  vec3 rd = normalize(vec3(p, -1.5));

  mat3 rot = rotY(uTime * uSpin * 0.3) * rotX((uTilt - 0.5) * PI * 0.6);
  ro = rot * ro;
  rd = rot * rd;

  int grid = int(clamp(uGridSize, 1.0, 8.0));
  float bestT = 1e9;
  vec3 bestN = vec3(0.0);
  vec3 bestCenter = vec3(0.0);
  float bestLuma = 0.0;
  float spc = uSpacing;
  float halfGrid = float(grid - 1) * 0.5 * spc;

  // Sample cubes uniformly: at each grid cell, sample_ source colour,
  // displace cube outward by luma * uHeight, then ray-test.
  for (int i = 0; i < 8; i++) {
    if (i >= grid) break;
    for (int j = 0; j < 8; j++) {
      if (j >= grid) break;
      for (int k = 0; k < 8; k++) {
        if (k >= grid) break;
        vec3 cellCenter = vec3(float(i) * spc - halfGrid,
                               float(j) * spc - halfGrid,
                               float(k) * spc - halfGrid);
        // Sample source at cell's projected XY
        vec2 sUv = vec2(float(i) / float(grid - 1), float(j) / float(grid - 1));
        vec3 c = IMG_NORM_PIXEL(inputImage, sUv).rgb;
        float l = luma(c);
        // Displace outward from origin proportional to luma
        vec3 dir = normalize(cellCenter + 1e-4);
        vec3 cubePos = cellCenter + dir * l * uHeight * 0.8;
        // Ray-cube test
        vec3 boxRO = ro - cubePos;
        vec3 N;
        float ti = intersectBox(boxRO, rd, vec3(uCubeSize), N);
        if (ti > 0.0 && ti < bestT) {
          bestT = ti;
          bestN = N;
          bestCenter = cubePos;
          bestLuma = l;
        }
      }
    }
  }

  if (bestT > 1e8) {
    gl_FragColor = vec4(uBgColor, 1.0 - uHorizonFade);
    return;
  }

  vec3 hit = ro + rd * bestT;
  vec3 localPos = hit - bestCenter;
  // Map face to source UV
  vec2 faceUv;
  vec3 an = abs(bestN);
  if (an.x > 0.5) faceUv = (localPos.zy / uCubeSize) * 0.5 + 0.5;
  else if (an.y > 0.5) faceUv = (localPos.xz / uCubeSize) * 0.5 + 0.5;
  else faceUv = (localPos.xy / uCubeSize) * 0.5 + 0.5;
  vec3 faceCol = IMG_NORM_PIXEL(inputImage, faceUv).rgb;

  vec3 lightDir = normalize(vec3(0.5, 0.7, 0.5));
  float diff = max(dot(bestN, lightDir), 0.0);
  float spec = pow(max(dot(reflect(-lightDir, bestN), -rd), 0.0), 32.0) * uSpecular;
  vec3 col = faceCol * (uAmbient + diff * 0.85) + vec3(spec);
  col += uBgColor * uAudioBass * 0.2;

  // Distance fade
  float fade = 1.0 - clamp((bestT - uCamDistance + 2.0) / 4.0, 0.0, 1.0);
  float alpha = mix(1.0, fade, uHorizonFade);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), alpha);
}
