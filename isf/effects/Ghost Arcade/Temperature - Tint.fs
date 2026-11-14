/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Warm/cool white balance with green-magenta tint. Adapted from Ghost Arcade's Temperature / Tint effect.",
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
            "NAME": "temperatureValue",
            "LABEL": "Temperature",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "tint",
            "LABEL": "Tint",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "shadowTemp",
            "LABEL": "Shadow Temp",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "highlightTemp",
            "LABEL": "Highlight Temp",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "splitTone",
            "LABEL": "Split Tone",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "autoCycle",
            "LABEL": "Auto Cycle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (temperatureTintHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uTemperature (temperatureValue)
#define uTint (tint)
#define uShadowTemp (shadowTemp)
#define uHighlightTemp (highlightTemp)
#define uSplitTone (splitTone)
#define uAutoCycle (autoCycle)

   // -1..+1 — cool (blue) to warm (orange)
          // -1..+1 — green to magenta
    // -1..+1 — split-tone shadows (used when uSplitTone > 0)
 // -1..+1 — split-tone highlights
     // 0-1 — blend factor between simple temp and split-tone
     // 0-1 — auto temperature oscillation amplitude

vec3 tempShift(float t) {
  // Approximate kelvin shift: warm = +R/-B, cool = -R/+B, slight G compensation.
  return vec3(t * 0.30, t * 0.05, -t * 0.30);
}
vec3 tintShift(float t) {
  // Green = +G/-RB, magenta = -G/+RB.
  return vec3(-t * 0.10, t * 0.18, -t * 0.10);
}

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 src = texColor.rgb;

  // Auto-cycle adds a slow sine-wave temperature offset.
  float autoT = sin(uTime * 0.4) * uAutoCycle * 0.5;

  // Single-temp path (uSplitTone=0).
  vec3 simple = src + tempShift(uTemperature + autoT) + tintShift(uTint);

  // Split-tone path: per-pixel temperature based on luminance.
  float lum = dot(src, vec3(0.299, 0.587, 0.114));
  float perPixT = mix(uShadowTemp, uHighlightTemp, smoothstep(0.0, 1.0, lum));
  vec3 split = src + tempShift(perPixT + autoT) + tintShift(uTint);

  vec3 result = mix(simple, split, uSplitTone);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), texColor.a);
}
