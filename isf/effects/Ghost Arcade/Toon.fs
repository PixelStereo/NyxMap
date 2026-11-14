/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Cel-shaded cartoon quantization. Adapted from Ghost Arcade's Toon effect.",
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
            "NAME": "stepsValue",
            "LABEL": "Steps",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "outlineValue",
            "LABEL": "Outline",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "outlineColor",
            "LABEL": "Outline Color",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "shadowBand",
            "LABEL": "Shadow Band",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "rampSoftness",
            "LABEL": "Ramp Softness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "colorPop",
            "LABEL": "Color Pop",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        }
    ]
}*/

// Ported from Ghost Arcade (toonHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uSteps (stepsValue)
#define uOutline (outlineValue)
#define uOutlineColor (outlineColor)
#define uShadowBand (shadowBand)
#define uRampSoftness (rampSoftness)
#define uColorPop (colorPop)

         // 2-8 quantization steps per channel
       // 0-1 outline strength
  // 0=black, 1=color from source
    // 0-1 darken shadow band intensity
  // 0-1 smooth ramp (0=hard cel, 1=soft)
      // 0-1 saturation boost

vec3 quantize(vec3 c, float steps, float soft) {
  vec3 hard = floor(c * steps) / max(steps - 1.0, 1.0);
  if (soft < 0.001) return hard;
  // Soft ramp: blend between hard and continuous via smoothstep on each
  // channel toward the next step.
  vec3 frac = fract(c * steps);
  vec3 smoothBlend = smoothstep(0.4, 0.6, frac);
  return mix(hard, hard + smoothBlend / max(steps - 1.0, 1.0), soft);
}

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 src = texColor.rgb;

  // Color pop pre-process — bumps saturation slightly.
  if (uColorPop > 0.001) {
    float lum = dot(src, vec3(0.299, 0.587, 0.114));
    src = mix(vec3(lum), src, 1.0 + uColorPop * 0.6);
  }

  vec3 quant = quantize(clamp(src, 0.0, 1.0), uSteps, uRampSoftness);

  // Shadow band — darken the lowest quantization step further.
  if (uShadowBand > 0.001) {
    float lum = dot(quant, vec3(0.299, 0.587, 0.114));
    float shadow = smoothstep(0.35, 0.0, lum);
    quant *= 1.0 - shadow * uShadowBand * 0.5;
  }

  // Built-in outline — Sobel-ish luma gradient.
  if (uOutline > 0.001) {
    vec2 t = 1.5 / uResolution;
    float lL = dot(IMG_NORM_PIXEL(inputImage, vUv - vec2(t.x, 0.0)).rgb, vec3(0.299, 0.587, 0.114));
    float lR = dot(IMG_NORM_PIXEL(inputImage, vUv + vec2(t.x, 0.0)).rgb, vec3(0.299, 0.587, 0.114));
    float lD = dot(IMG_NORM_PIXEL(inputImage, vUv - vec2(0.0, t.y)).rgb, vec3(0.299, 0.587, 0.114));
    float lU = dot(IMG_NORM_PIXEL(inputImage, vUv + vec2(0.0, t.y)).rgb, vec3(0.299, 0.587, 0.114));
    float edge = clamp(length(vec2(lR - lL, lU - lD)) * 6.0, 0.0, 1.0);
    vec3 lineCol = mix(vec3(0.0), src * 0.4, uOutlineColor);
    quant = mix(quant, lineCol, edge * uOutline);
  }

  gl_FragColor = vec4(quant, texColor.a);
}
