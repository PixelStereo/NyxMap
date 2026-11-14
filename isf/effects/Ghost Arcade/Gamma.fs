/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Gamma curve brightness correction. Adapted from Ghost Arcade's Gamma effect.",
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
            "NAME": "shadows",
            "LABEL": "Shadows",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "mids",
            "LABEL": "Mids",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "highlightsValue",
            "LABEL": "Highlights",
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

// Ported from Ghost Arcade (gammaHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uShadows (shadows)
#define uMids (mids)
#define uHighlights (highlightsValue)
#define uMix (mixValue)

     // 0.2-3.0 gamma for shadow region
        // 0.2-3.0 gamma for midtones
  // 0.2-3.0 gamma for highlights
         // 0-1 wet/dry

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 src = texColor.rgb;
  float lum = dot(src, vec3(0.299, 0.587, 0.114));
  // Three smoothstep weights centered on 0.15 / 0.5 / 0.85.
  float wS = 1.0 - smoothstep(0.0, 0.5, lum);
  float wH = smoothstep(0.5, 1.0, lum);
  float wM = 1.0 - wS - wH;
  // Blend the three gamma curves additively by weight.
  vec3 gS = pow(src, vec3(max(uShadows, 0.0001)));
  vec3 gM = pow(src, vec3(max(uMids, 0.0001)));
  vec3 gH = pow(src, vec3(max(uHighlights, 0.0001)));
  vec3 graded = gS * wS + gM * wM + gH * wH;
  gl_FragColor = vec4(mix(src, graded, uMix), texColor.a);
}
