/*{
    "ISFVSN": "2",
    "DESCRIPTION": "RGB chromatic dispersion through a triangular prism with rainbow caustics. Adapted from Ghost Arcade's Prism Split effect.",
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
            "LABEL": "Separation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Prism Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount3",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "threshold",
            "LABEL": "Glass Tint",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "angleValue",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (prismSplitShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(mode + 0.5))
#define uAmount (amount)
#define uAmount2 (amount2)
#define uAmount3 (amount3)
#define uThreshold (threshold)
#define uAngle (angleValue)
#define uCenter vec2(0.5, 0.5)
#define uColor vec3(0.5, 0.5, 0.5)

#define PI 3.14159265359
#define TAU 6.28318530718

mat2 rot2(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }

// Signed distance to a triangle prism cross-section (2D)
float sdTriangle(vec2 p, float r) {
  float k = sqrt(3.0);
  p.x = abs(p.x) - r;
  p.y = p.y + r / k;
  if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
  p.x -= clamp(p.x, -2.0 * r, 0.0);
  return -length(p) * sign(p.y);
}

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float separation = uAmount * 0.15;
  float prismAngle = uAmount2 * PI;
  float rotation = uAmount3 * TAU;
  float tintIntensity = uThreshold;

  vec2 p = (uv - 0.5) * 2.0;
  p.x *= uResolution.x / uResolution.y;

  float t = uTime;

  // Rotate space
  p = rot2(rotation + t * 0.3) * p;

  // Prism shape
  float prismSize = 0.4 + prismAngle * 0.3;
  float dist = sdTriangle(p, prismSize);

  // Amount of refraction based on distance to prism
  float inPrism = smoothstep(0.02, -0.02, dist);

  // Each color channel gets a different refraction angle
  float baseAngle = atan(p.y, p.x);
  float baseDist = length(p);

  // Chromatic separation offsets
  vec2 redOffset = vec2(cos(baseAngle - separation), sin(baseAngle - separation)) * separation * baseDist;
  vec2 greenOffset = vec2(0.0);
  vec2 blueOffset = vec2(cos(baseAngle + separation), sin(baseAngle + separation)) * separation * baseDist;

  // Scale up separation when inside prism
  redOffset *= (1.0 + inPrism * 4.0);
  blueOffset *= (1.0 + inPrism * 4.0);

  // Sample each channel with offset
  float r = IMG_NORM_PIXEL(inputImage, uv + redOffset).r;
  float g = IMG_NORM_PIXEL(inputImage, uv + greenOffset).g;
  float b = IMG_NORM_PIXEL(inputImage, uv + blueOffset).b;

  vec3 col = vec3(r, g, b);

  // Glass tint from prism
  vec3 tint = mix(vec3(1.0), uColor, tintIntensity * inPrism);
  col *= tint;

  // Edge highlight (caustic-like on prism edges)
  float edgeGlow = smoothstep(0.05, 0.0, abs(dist)) * 0.6;
  // Rainbow on edge
  vec3 rainbow = 0.5 + 0.5 * cos(TAU * (baseAngle / TAU + vec3(0.0, 0.33, 0.67)));
  col += rainbow * edgeGlow * separation * 5.0;

  // Subtle prism surface reflection
  float highlight = pow(max(1.0 - abs(dist) * 5.0, 0.0), 8.0) * 0.2;
  col += vec3(highlight);

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
