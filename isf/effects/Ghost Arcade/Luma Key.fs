/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Remove pixels by brightness threshold. Adapted from Ghost Arcade's Luma Key effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Keying",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "lowCut",
            "LABEL": "Low Cut",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "highCut",
            "LABEL": "High Cut",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "invert",
            "LABEL": "Invert",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "gammaValue",
            "LABEL": "Gamma",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "matteValue",
            "LABEL": "Matte",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "premultiply",
            "LABEL": "Premultiply",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (lumaKeyHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uLowCut (lowCut)
#define uHighCut (highCut)
#define uInvert (invert)
#define uGamma (gammaValue)
#define uMatte (matteValue)
#define uPremultiply (premultiply)

        // 0-1 fade-in start
       // 0-1 fade-in end
        // 0=keep bright, 1=keep dark
         // 0.2-3 matte gamma
         // 0=normal, 1=show matte
   // 0=straight alpha, 1=premultiplied

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  float l = luma(src);
  float matte = smoothstep(uLowCut, max(uLowCut + 0.001, uHighCut), l);
  if (uInvert > 0.5) matte = 1.0 - matte;
  matte = pow(clamp(matte, 0.0, 1.0), max(0.001, uGamma));

  if (uMatte > 0.5) {
    gl_FragColor = vec4(vec3(matte), 1.0);
  } else {
    vec3 result = (uPremultiply > 0.5) ? src * matte : src;
    gl_FragColor = vec4(result, matte);
  }
}
