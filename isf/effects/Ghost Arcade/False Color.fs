/*{
    "ISFVSN": "2",
    "DESCRIPTION": "IRE exposure zebra — visualize under/overexposure. Adapted from Ghost Arcade's False Color effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Color",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "mixValue",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "showOriginal",
            "LABEL": "Show Original",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "midpointValue",
            "LABEL": "Midpoint",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "range",
            "LABEL": "Range",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (falseColorHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(modeValue + 0.5))
#define uMix (mixValue)
#define uShowOriginal (showOriginal)
#define uMidpoint (midpointValue)
#define uRange (range)

          // 0=DIT exposure, 1=zone heat, 2=Resolve, 3=histogram
           // 0-1
  // 0-1 fade overlay vs replace
      // 0-1 reference midtone (0.5 default)
         // 0.05-0.5 zone width

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 ditExposure(float l) {
  if (l < 0.04) return vec3(0.5, 0, 0.7);  // Purple — clip black
  if (l < 0.18) return vec3(0, 0, 0.9);    // Blue — shadows
  if (l < 0.42) return vec3(0, 0.8, 0.6);  // Teal — low-mid
  if (l < 0.55) return vec3(0.4, 0.8, 0);  // Green — midtone (safe)
  if (l < 0.7)  return vec3(1, 1, 0);      // Yellow — high-mid
  if (l < 0.92) return vec3(1, 0.5, 0);    // Orange — highlights
  return vec3(1, 0, 0);                    // Red — clip white
}

vec3 zoneHeat(float l) {
  // Adams zone system 0-X mapped to 7-color gradient
  float n = l;
  return mix(
    mix(vec3(0,0,0.5), vec3(0,0.7,1), smoothstep(0.0, 0.4, n)),
    mix(vec3(0,1,0), vec3(1,1,0), smoothstep(0.4, 0.7, n)),
    smoothstep(0.4, 0.55, n)
  ) + smoothstep(0.85, 1.0, n) * vec3(1, 0.2, 0);
}

vec3 resolveStyle(float l) {
  // Two-color highlight/shadow warning (blue = underexposed, red = overexposed)
  if (l < 0.05) return vec3(0, 0, 1);
  if (l > 0.95) return vec3(1, 0, 0);
  return vec3(l); // grayscale otherwise
}

vec3 histogramStripes(float l) {
  // Map exposure to rainbow stripes for histogram-style preview
  float h = l;
  return vec3(
    sin(h * 9.42) * 0.5 + 0.5,
    sin(h * 9.42 + 2.094) * 0.5 + 0.5,
    sin(h * 9.42 + 4.189) * 0.5 + 0.5
  );
}

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  float l = luma(src.rgb);
  int mode = int(uMode + 0.5);
  vec3 fc;
  if (mode == 0) fc = ditExposure(l);
  else if (mode == 1) fc = zoneHeat(l);
  else if (mode == 2) fc = resolveStyle(l);
  else fc = histogramStripes(l);

  // Highlight zones around midpoint
  if (uRange > 0.001) {
    float zoneMask = smoothstep(uRange, 0.0, abs(l - uMidpoint));
    fc = mix(fc, vec3(0, 1, 0), zoneMask * 0.4); // green tint on safe zone
  }

  vec3 result = mix(src.rgb, fc, uShowOriginal);
  gl_FragColor = vec4(mix(src.rgb, result, uMix), src.a);
}
