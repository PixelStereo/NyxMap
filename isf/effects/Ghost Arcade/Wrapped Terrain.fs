/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Source image extruded as luma displacement around a sphere, cube, or pyramid. Auto-rotate + Fresnel silhouette fade + tile scale. Adapted from Ghost Arcade's Wrapped Terrain effect.",
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
            "NAME": "shapeValue",
            "LABEL": "Shape",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "heightValue",
            "LABEL": "Height",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "rotateX",
            "LABEL": "Rotate X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "rotateY",
            "LABEL": "Rotate Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "autoRotate",
            "LABEL": "Auto Rotate",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "camDistance",
            "LABEL": "Cam Distance",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 5.0,
            "DEFAULT": 2.5
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
            "NAME": "fogDistance",
            "LABEL": "Fog Distance",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "horizonFade",
            "LABEL": "Horizon Fade",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "sourceMix",
            "LABEL": "Source Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
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

// Ported from Ghost Arcade (wrappedTerrainShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uShape float(floor(shapeValue + 0.5))
#define uHeight (heightValue)
#define uRotateX (rotateX)
#define uRotateY (rotateY)
#define uAutoRotate (autoRotate)
#define uCamDistance (camDistance)
#define uSpecular (specular)
#define uAmbient (ambient)
#define uFogDistance (fogDistance)
#define uFogColor vec3(0.05, 0.07, 0.12)
#define uHorizonFade (horizonFade)
#define uSourceMix (sourceMix)
#define uTileScale (tileScale)

         // 0=sphere, 1=cube, 2=pyramid
        // 0-1 extrusion amount

#define PI 3.14159265359
#define TAU 6.28318530718

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
mat3 rotX(float a) { float s = sin(a), c = cos(a); return mat3(1,0,0, 0,c,-s, 0,s,c); }
mat3 rotY(float a) { float s = sin(a), c = cos(a); return mat3(c,0,s, 0,1,0, -s,0,c); }

// SDF for an axis-aligned box.
float sdBox(vec3 p, vec3 b) {
  vec3 d = abs(p) - b;
  return length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);
}

// SDF for a 4-sided pyramid (apex up). h = height. baseHalf = base half-width.
float sdPyramid(vec3 p, float h, float baseHalf) {
  p.y += baseHalf * 0.4;
  p.xz = abs(p.xz);
  float m2 = h * h + baseHalf * baseHalf;
  vec3 q = vec3(p.z, h * p.y - baseHalf * p.x, h * p.x + baseHalf * p.y);
  float s = max(-q.x, 0.0);
  float t = clamp((q.y - baseHalf * p.z) / (m2 + baseHalf * baseHalf), 0.0, 1.0);
  float a = m2 * (q.x + s) * (q.x + s) + q.y * q.y;
  float b = m2 * (q.x + 0.5 * t) * (q.x + 0.5 * t) + (q.y - m2 * t) * (q.y - m2 * t);
  float d2 = min(q.y, -q.x * m2 - q.y * baseHalf) > 0.0 ? 0.0 : min(a, b);
  return sqrt((d2 + q.z * q.z) / m2) * sign(max(q.z, -p.y));
}

// Map a point in surface space to a UV coordinate based on shape.
vec2 surfaceUV(vec3 p, int shape) {
  if (shape == 0) {
    // Sphere — equirectangular
    vec3 d = normalize(p + 1e-6);
    return vec2(atan(d.z, d.x) / TAU + 0.5, asin(clamp(d.y, -1.0, 1.0)) / PI + 0.5);
  } else if (shape == 1) {
    // Cube — pick face from largest |axis|, project remaining two
    vec3 ap = abs(p);
    vec2 uv;
    if (ap.x > ap.y && ap.x > ap.z) {
      uv = vec2(p.z * sign(p.x), p.y) * 0.5 + 0.5;
    } else if (ap.y > ap.z) {
      uv = vec2(p.x, p.z * sign(p.y)) * 0.5 + 0.5;
    } else {
      uv = vec2(p.x * sign(p.z), p.y) * 0.5 + 0.5;
    }
    return clamp(uv, 0.0, 1.0);
  } else {
    // Pyramid — wrap U around base, V vertical
    vec3 d = normalize(p + 1e-6);
    return vec2(atan(d.z, d.x) / TAU + 0.5, p.y * 0.5 + 0.5);
  }
}

// Sample heightfield from source luma at given surface point.
float heightAt(vec3 p, int shape) {
  vec2 uv = fract(surfaceUV(p, shape) * uTileScale);
  return luma(IMG_NORM_PIXEL(inputImage, uv).rgb);
}

// Bumped SDF — base shape minus luma height along outward direction.
float bumpedSDF(vec3 p) {
  int shape = int(uShape + 0.5);
  float baseR = 0.85;
  float h = heightAt(p, shape) * uHeight * 0.5;
  float baseSDF;
  if (shape == 0) {
    baseSDF = length(p) - baseR;
  } else if (shape == 1) {
    baseSDF = sdBox(p, vec3(baseR));
  } else {
    baseSDF = sdPyramid(p, baseR * 1.2, baseR);
  }
  return baseSDF - h;
}

// 4-tap normal estimation
vec3 calcNormal(vec3 p) {
  const vec2 e = vec2(0.0025, -0.0025);
  return normalize(
    e.xyy * bumpedSDF(p + e.xyy) +
    e.yyx * bumpedSDF(p + e.yyx) +
    e.yxy * bumpedSDF(p + e.yxy) +
    e.xxx * bumpedSDF(p + e.xxx)
  );
}

void main() {
  vec3 srcRaw = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec2 screenPos = vUv * 2.0 - 1.0;
  screenPos.x *= uResolution.x / uResolution.y;

  float dist = max(1.5, uCamDistance);
  vec3 ro = vec3(0.0, 0.0, dist);
  vec3 rd = normalize(vec3(screenPos, -1.5));

  float angY = uRotateY * TAU + uTime * uAutoRotate * 0.4;
  float angX = (uRotateX - 0.5) * PI;
  mat3 rotMat = rotY(angY) * rotX(angX);
  ro = rotMat * ro;
  rd = rotMat * rd;

  // SDF raymarch
  float t = 0.0;
  bool hit = false;
  for (int i = 0; i < 80; i++) {
    vec3 p = ro + rd * t;
    float d = bumpedSDF(p);
    if (d < 0.001) { hit = true; break; }
    t += max(d * 0.7, 0.005);
    if (t > 8.0) break;
  }

  if (!hit) {
    float alpha = 1.0 - uHorizonFade;
    vec3 col = mix(uFogColor, srcRaw, uSourceMix);
    gl_FragColor = vec4(col, alpha);
    return;
  }

  vec3 hitPos = ro + rd * t;
  vec3 N = calcNormal(hitPos);
  int shape = int(uShape + 0.5);
  vec2 sUV = fract(surfaceUV(hitPos, shape) * uTileScale);
  vec3 texCol = IMG_NORM_PIXEL(inputImage, sUV).rgb;

  // Lighting
  vec3 lightDir = normalize(rotMat * vec3(0.5, 0.7, 0.5));
  float diff = max(dot(N, lightDir), 0.0);
  float spec = pow(max(dot(reflect(-lightDir, N), -rd), 0.0), 32.0) * uSpecular;
  vec3 lit = texCol * (uAmbient + diff * 0.85) + vec3(spec);

  // Distance fog
  float fogFactor = 1.0 - exp(-t * uFogDistance * 0.3);
  lit = mix(lit, uFogColor, fogFactor);

  // Fresnel-like silhouette fade for clean transparent BG
  float fres = 1.0 - max(dot(N, -rd), 0.0);
  float silAlpha = 1.0 - smoothstep(0.7, 1.0, fres) * uHorizonFade;

  vec3 disp = mix(lit, srcRaw, uSourceMix);
  gl_FragColor = vec4(clamp(disp, 0.0, 1.0), silAlpha);
}
