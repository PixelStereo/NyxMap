/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Shift color balance across RGB channels. Adapted from Ghost Arcade's Color Balance effect.",
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
            "NAME": "shadowR",
            "LABEL": "Shadow R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "shadowG",
            "LABEL": "Shadow G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "shadowB",
            "LABEL": "Shadow B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "midR",
            "LABEL": "Mid R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "midG",
            "LABEL": "Mid G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "midB",
            "LABEL": "Mid B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "highR",
            "LABEL": "High R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "highG",
            "LABEL": "High G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "highB",
            "LABEL": "High B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "preserveLuma",
            "LABEL": "Preserve Luma",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "mixValue",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (colorBalanceHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uShadowR (shadowR)
#define uShadowG (shadowG)
#define uShadowB (shadowB)
#define uMidR (midR)
#define uMidG (midG)
#define uMidB (midB)
#define uHighR (highR)
#define uHighG (highG)
#define uHighB (highB)
#define uPreserveLuma (preserveLuma)
#define uMix (mixValue)

               // -1..+1 each
                
              
// 0-1 keep luma stable while shifting hue

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 src = texColor.rgb;
  float lum = dot(src, vec3(0.299, 0.587, 0.114));

  // Three smoothstep weights matching Gamma hero's three-zone setup.
  float wS = 1.0 - smoothstep(0.0, 0.5, lum);
  float wH = smoothstep(0.5, 1.0, lum);
  float wM = 1.0 - wS - wH;

  vec3 shift = vec3(uShadowR, uShadowG, uShadowB) * wS * 0.3
             + vec3(uMidR,    uMidG,    uMidB)    * wM * 0.3
             + vec3(uHighR,   uHighG,   uHighB)   * wH * 0.3;

  vec3 graded = src + shift;

  if (uPreserveLuma > 0.001) {
    float newLum = dot(graded, vec3(0.299, 0.587, 0.114));
    graded += (lum - newLum) * uPreserveLuma;
  }

  gl_FragColor = vec4(clamp(mix(src, graded, uMix), 0.0, 1.0), texColor.a);
}
