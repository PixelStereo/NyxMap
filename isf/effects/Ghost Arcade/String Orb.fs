/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Wire-wrapped luminous globe sampling source along latitude + longitude + diagonal strings with rim glow + audio hooks. Adapted from Ghost Arcade's String Orb effect.",
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
            "NAME": "radiusValue",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
        },
        {
            "NAME": "height",
            "LABEL": "Height",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "latCount",
            "LABEL": "Lat Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 32.0,
            "DEFAULT": 16.0
        },
        {
            "NAME": "lonCount",
            "LABEL": "Lon Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 48.0,
            "DEFAULT": 24.0
        },
        {
            "NAME": "diagCount",
            "LABEL": "Diag Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 16.0,
            "DEFAULT": 8.0
        },
        {
            "NAME": "slope",
            "LABEL": "Slope",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.5
        },
        {
            "NAME": "width",
            "LABEL": "Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.012
        },
        {
            "NAME": "spinValue",
            "LABEL": "Spin",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "tiltValue",
            "LABEL": "Tilt",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.15
        },
        {
            "NAME": "flow",
            "LABEL": "Flow",
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
            "NAME": "glow",
            "LABEL": "Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "horizonFade",
            "LABEL": "Horizon Fade",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "tileScale",
            "LABEL": "Tile Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (stringOrbShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uRadius (radiusValue)
#define uHeight (height)
#define uLatCount (latCount)
#define uLonCount (lonCount)
#define uDiagCount (diagCount)
#define uSlope (slope)
#define uWidth (width)
#define uSpin (spinValue)
#define uTilt (tiltValue)
#define uFlow (flow)
#define uIntensity (intensity)
#define uGlow (glow)
#define uGlowColor vec3(0.4, 0.85, 1.0)
#define uHorizonFade (horizonFade)
#define uTileScale (tileScale)
#define uAudioBass 0.0
#define uAudioHigh 0.0
#define uAudioBeatPulse 0.0

        // 0.6-1.4 base sphere radius
        // 0-1 luma extrusion
      // 4-64
      // 4-64
     // 0-32
         // -2..2
         // 0.005-0.05
          // -3..3
          // 0-1
          // 0-3
     // 0-2
          // 0-2

   // 0-1
     // 0.5-4


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


// Bumped sphere SDF — surface = baseR + luma(equirectUV(dir)) * heightScale
float bumpedSphereSDF(vec3 p) {
  vec3 d = normalize(p + 1e-6);
  vec2 uv = sphereUV(d);
  uv = fract(uv * uTileScale);
  float h = luma(IMG_NORM_PIXEL(inputImage, uv).rgb) * uHeight * 0.4;
  return length(p) - (uRadius + h);
}

vec3 calcNormal(vec3 p) {
  const vec2 e = vec2(0.003, -0.003);
  return normalize(
    e.xyy * bumpedSphereSDF(p + e.xyy) +
    e.yyx * bumpedSphereSDF(p + e.yyx) +
    e.yxy * bumpedSphereSDF(p + e.yxy) +
    e.xxx * bumpedSphereSDF(p + e.xxx)
  );
}

void main() {
  vec3 srcRaw = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec2 p = (gl_FragCoord.xy * 2.0 - uResolution.xy) / uResolution.y;
  vec3 ro = vec3(0.0, 0.0, 2.8);
  vec3 rd = normalize(vec3(p, -1.8));

  // Auto-spin + tilt
  mat3 rot = rotY(uTime * uSpin) * rotX(uTilt * PI * 0.5);
  ro = rot * ro;
  rd = rot * rd;

  // SDF raymarch
  float t = 0.0;
  bool hit = false;
  for (int i = 0; i < 80; i++) {
    vec3 pp = ro + rd * t;
    float d = bumpedSphereSDF(pp);
    if (d < 0.001) { hit = true; break; }
    t += max(d * 0.7, 0.005);
    if (t > 6.0) break;
  }

  if (!hit) {
    float a = 1.0 - uHorizonFade;
    gl_FragColor = vec4(uGlowColor * 0.05, a);
    return;
  }

  vec3 hitPos = ro + rd * t;
  vec3 N = calcNormal(hitPos);
  vec3 dir = normalize(hitPos);
  vec2 suv = sphereUV(dir);
  vec2 sUVtile = fract(suv * uTileScale);

  // Audio modulates string density slightly (high freqs tighten lattice)
  float lat = 1.0 - smoothstep(uWidth, uWidth * 2.0,
    abs(fract(suv.y * uLatCount * (1.0 + uAudioHigh * 0.2)) - 0.5));
  float lon = 1.0 - smoothstep(uWidth, uWidth * 2.0,
    abs(fract(suv.x * uLonCount * (1.0 + uAudioHigh * 0.2)) - 0.5));
  float diag = (uDiagCount > 0.5)
    ? 1.0 - smoothstep(uWidth, uWidth * 2.0,
        abs(fract((suv.x + suv.y * uSlope + uTime * uFlow) * uDiagCount) - 0.5))
    : 0.0;
  float strings = max(max(lat, lon), diag);

  vec3 tex = IMG_NORM_PIXEL(inputImage, sUVtile).rgb;
  float rim = fresnel(N, rd, 2.2);
  vec3 col = tex * strings * uIntensity
           + uGlowColor * (rim + strings * strings) * uGlow
           + uGlowColor * uAudioBass * 0.4;

  float alpha = max(strings * 0.95, rim * uHorizonFade) + uHorizonFade * 0.05;
  alpha = clamp(alpha, 0.0, 1.0);
  gl_FragColor = vec4(col, alpha);
}
