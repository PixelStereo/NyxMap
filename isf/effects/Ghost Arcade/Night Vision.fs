/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Military night vision green phosphor look. Adapted from Ghost Arcade's Night Vision effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Color",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "nightVisionIntensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "nightVisionNoise",
            "LABEL": "Noise",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "vignetteValue",
            "LABEL": "Vignette",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "phosphorValue",
            "LABEL": "Phosphor",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "bloomValue",
            "LABEL": "Bloom",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "scopeMask",
            "LABEL": "Scope Mask",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "rollingNoise",
            "LABEL": "Rolling Noise",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (nightVisionShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (nightVisionIntensity)
#define uNoise (nightVisionNoise)
#define uVignette (vignetteValue)
#define uPhosphor (phosphorValue)
#define uBloom (bloomValue)
#define uScopeMask (scopeMask)
#define uRollingNoise (rollingNoise)

     // 0-2 brightness boost
         // 0-1 grain amount
      // 0-1 circular vignette intensity
// Hero-rewrite params
      // 0=green, 1=amber, 2=white phosphor
         // 0-2 phosphor bloom strength
     // 0=off, 1=circle (legacy), 2=scope crosshairs
  // 0-1 horizontal rolling noise band amplitude

float nvRandom(vec2 st) {
  return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

vec3 phosphorTint(float lum, int phosphor) {
  if (phosphor == 0) return vec3(lum * 0.2, lum, lum * 0.2);   // green
  if (phosphor == 1) return vec3(lum, lum * 0.65, lum * 0.15); // amber (1980s NVGs)
  return vec3(lum);                                             // white phosphor (modern)
}

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);

  float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
  lum = pow(lum, 0.8) * uIntensity;

  int phos = int(uPhosphor + 0.5);
  vec3 nvColor = phosphorTint(lum, phos);

  // Scanlines (always on at low intensity).
  float scanline = sin(vUv.y * uResolution.y * 2.0) * 0.5 + 0.5;
  nvColor *= 0.95 + scanline * 0.05;

  // Rolling noise — horizontal bands that scroll downward over time.
  if (uRollingNoise > 0.001) {
    float bandY = floor((vUv.y + uTime * 0.15) * 80.0);
    float bandRand = nvRandom(vec2(bandY, floor(uTime * 4.0)));
    nvColor += vec3(bandRand - 0.5) * uRollingNoise * 0.4 * vec3(0.0, 1.0, 0.0);
  }

  // Sensor grain.
  float n = nvRandom(vUv * uResolution + uTime * 1000.0);
  nvColor += (n - 0.5) * uNoise * 0.2;

  // Phosphor bloom — ring sample_ around the pixel, weighted by tint.
  if (uBloom > 0.001) {
    float glowSum = 0.0;
    for (int i = -2; i <= 2; i++) {
      for (int j = -2; j <= 2; j++) {
        vec2 offset = vec2(float(i), float(j)) / uResolution * (3.0 + uBloom * 2.0);
        float s = dot(IMG_NORM_PIXEL(inputImage, vUv + offset).rgb, vec3(0.299, 0.587, 0.114));
        glowSum += s;
      }
    }
    glowSum /= 25.0;
    vec3 bloomTint = phosphorTint(glowSum * 0.5, phos);
    nvColor += bloomTint * uBloom;
  }

  // Scope mask.
  int scope = int(uScopeMask + 0.5);
  if (scope >= 1) {
    vec2 center = vec2(0.5);
    float dist = length(vUv - center);
    float vig = 1.0 - smoothstep(0.3, 0.7, dist * (1.0 + uVignette));
    float scopeEdge = smoothstep(0.48, 0.5, dist);
    vig *= 1.0 - scopeEdge;
    nvColor *= vig;
    // Crosshairs overlay for mode 2.
    if (scope == 2) {
      float cx = abs(vUv.x - 0.5);
      float cy = abs(vUv.y - 0.5);
      float cross = step(cx, 0.001) + step(cy, 0.001);
      // Tick marks every 0.05.
      float tick = step(mod(vUv.y, 0.05), 0.002) * step(cx, 0.012)
                 + step(mod(vUv.x, 0.05), 0.002) * step(cy, 0.012);
      nvColor = mix(nvColor, phosphorTint(0.85, phos), min(cross + tick, 1.0));
    }
  }

  gl_FragColor = vec4(clamp(nvColor, 0.0, 1.0), texColor.a);
}
