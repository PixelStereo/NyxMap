/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Oil painting with quantized color bands. Adapted from Ghost Arcade's Oil Paint effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Stylize",
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
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
        },
        {
            "NAME": "brushLength",
            "LABEL": "Brush Length",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "bristleValue",
            "LABEL": "Bristle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "colorPunch",
            "LABEL": "Color Punch",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "highlight",
            "LABEL": "Highlight",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "mode",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (oilPaintHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uRadius (radiusValue)
#define uIntensity (intensity)
#define uBrushLength (brushLength)
#define uBristle (bristleValue)
#define uColorPunch (colorPunch)
#define uHighlight (highlight)
#define uMode float(floor(mode + 0.5))

        // 1-8 brush radius
     // 4-32 quantization bins
   // 0-2 directional brush length
       // 0-1 bristle striations
    // 0-1 saturation pop
     // 0-1 wet specular pop on bright bins
          // 0=bin pick, 1=variance pick (kuwahara-style)

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec2 texel = 1.0 / uResolution;
  int radius = int(clamp(uRadius, 1.0, 8.0));
  float bins = max(2.0, uIntensity);
  int binCount = 24;

  float intensityCount[24];
  vec3  averageColor[24];
  for (int i = 0; i < 24; i++) { intensityCount[i] = 0.0; averageColor[i] = vec3(0.0); }

  // Brush direction = local gradient (sobel on luma)
  float gx = 0.0; float gy = 0.0;
  {
    float l00 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(-1.0, -1.0)).rgb);
    float l10 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 0.0, -1.0)).rgb);
    float l20 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 1.0, -1.0)).rgb);
    float l01 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(-1.0,  0.0)).rgb);
    float l21 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 1.0,  0.0)).rgb);
    float l02 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(-1.0,  1.0)).rgb);
    float l12 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 0.0,  1.0)).rgb);
    float l22 = luma(IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 1.0,  1.0)).rgb);
    gx = (l20 + 2.0 * l21 + l22) - (l00 + 2.0 * l01 + l02);
    gy = (l02 + 2.0 * l12 + l22) - (l00 + 2.0 * l10 + l20);
  }
  float gradMag = length(vec2(gx, gy));
  vec2 brushDir = (gradMag > 0.001) ? vec2(-gy, gx) / gradMag : vec2(1.0, 0.0);

  for (int y = -8; y <= 8; y++) {
    if (abs(y) > radius) continue;
    for (int x = -8; x <= 8; x++) {
      if (abs(x) > radius) continue;
      vec2 sampleUv = vUv + vec2(float(x), float(y)) * texel * (1.0 + uBrushLength * abs(dot(normalize(vec2(float(x), float(y)) + 1e-6), brushDir)));
      vec3 c = IMG_NORM_PIXEL(inputImage, sampleUv).rgb;
      // Bristle striation: subtly modulate sample_ weight along brush dir
      float bristleMod = 1.0;
      if (uBristle > 0.001) {
        float along = dot(vec2(float(x), float(y)), brushDir);
        bristleMod = mix(1.0, 0.5 + 0.5 * sin(along * 3.14159 * 2.0), uBristle);
      }
      int bin = int(luma(c) * (bins - 1.0));
      bin = int(clamp(float(bin), 0.0, float(binCount - 1)));
      intensityCount[bin] += bristleMod;
      averageColor[bin] += c * bristleMod;
    }
  }

  int maxIdx = 0;
  float maxCount = 0.0;
  for (int i = 0; i < 24; i++) {
    if (intensityCount[i] > maxCount) { maxCount = intensityCount[i]; maxIdx = i; }
  }
  vec3 result = averageColor[maxIdx] / max(maxCount, 1.0);

  if (uColorPunch > 0.001) {
    float lum = luma(result);
    result = mix(vec3(lum), result, 1.0 + uColorPunch * 0.6);
  }
  if (uHighlight > 0.001) {
    float lum = luma(result);
    float spec = smoothstep(0.7, 0.95, lum) * uHighlight;
    result += vec3(spec);
  }

  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), src.a);
}
