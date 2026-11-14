/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Refractive blob lens with chromatic caustics + specular highlights. Up to 8 animated blobs with tint. Adapted from Ghost Arcade's Liquid Glass effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "New Hero",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "blobsValue",
            "LABEL": "Blobs",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 6.0,
            "DEFAULT": 3.0
        },
        {
            "NAME": "blobSize",
            "LABEL": "Blob Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.18
        },
        {
            "NAME": "refraction",
            "LABEL": "Refraction",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "chromatic",
            "LABEL": "Chromatic",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "specular",
            "LABEL": "Specular",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "causticAmount",
            "LABEL": "Caustic Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.9,
            "DEFAULT": 0.95
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (liquidGlassHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uBlobs (blobsValue)
#define uBlobSize (blobSize)
#define uRefraction (refraction)
#define uChromatic (chromatic)
#define uSpecular (specular)
#define uCausticAmount (causticAmount)
#define uSpeed (speed)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)

         // 1-8
      // 0.05-0.4
    // 0-1
     // 0-1
      // 0-1
 // 0-1
         // 0-3

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

// Compute summed signed-distance to N moving blobs
float blobField(vec2 uv, out vec2 grad) {
  int n = int(clamp(uBlobs, 1.0, 8.0));
  float sum = 0.0;
  grad = vec2(0.0);
  for (int i = 0; i < 8; i++) {
    if (i >= n) break;
    float fi = float(i);
    vec2 c = vec2(
      0.5 + 0.35 * sin(uTime * uSpeed * 0.3 + fi * 1.7),
      0.5 + 0.35 * cos(uTime * uSpeed * 0.4 + fi * 2.3)
    );
    vec2 d = uv - c;
    d.x *= uResolution.x / uResolution.y;
    float r = length(d);
    float w = exp(-r * r / (uBlobSize * uBlobSize));
    sum += w;
    grad -= d * w / (uBlobSize * uBlobSize) * 2.0;
  }
  return sum;
}

void main() {
  vec2 grad;
  float field = blobField(vUv, grad);
  float blob = smoothstep(0.7, 1.3, field);

  // Refract: bend rays inversely proportional to gradient
  vec2 refractDir = -grad * uRefraction * 0.04;
  vec3 col;
  if (uChromatic > 0.001) {
    col.r = IMG_NORM_PIXEL(inputImage, vUv + refractDir * (1.0 + uChromatic * 0.5)).r;
    col.g = IMG_NORM_PIXEL(inputImage, vUv + refractDir).g;
    col.b = IMG_NORM_PIXEL(inputImage, vUv + refractDir * (1.0 - uChromatic * 0.5)).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, vUv + refractDir).rgb;
  }
  col *= mix(vec3(1.0), vec3(uTintR, uTintG, uTintB), blob * 0.5);

  // Specular highlight on top of blob (top-left bias)
  if (uSpecular > 0.001) {
    vec3 N = normalize(vec3(grad, 1.0));
    vec3 L = normalize(vec3(-0.4, -0.6, 0.6));
    float spec = pow(max(0.0, dot(N, L)), 32.0);
    col += vec3(spec) * uSpecular * blob * 1.5;
  }

  // Caustics outside blob
  if (uCausticAmount > 0.001) {
    float caust = (1.0 - blob) * (sin(field * 30.0 + uTime) * 0.5 + 0.5);
    col += vec3(uCausticAmount) * vec3(uTintR, uTintG, uTintB) * caust * 0.4;
  }

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
