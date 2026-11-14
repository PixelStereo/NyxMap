/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Independent soft feathering on each edge. Adapted from Ghost Arcade's Edge Feather effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Masking",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "featherTop",
            "LABEL": "Top",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 0.5,
            "DEFAULT": 0.0
        },
        {
            "NAME": "featherBottom",
            "LABEL": "Bottom",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 0.5,
            "DEFAULT": 0.0
        },
        {
            "NAME": "featherLeft",
            "LABEL": "Left",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 0.5,
            "DEFAULT": 0.0
        },
        {
            "NAME": "featherRight",
            "LABEL": "Right",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 0.5,
            "DEFAULT": 0.0
        },
        {
            "NAME": "featherSoftness",
            "LABEL": "Softness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "gamma",
            "LABEL": "Gamma",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "mattePreview",
            "LABEL": "Matte Preview",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (edgeFeatherShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uTop (featherTop)
#define uBottom (featherBottom)
#define uLeft (featherLeft)
#define uRight (featherRight)
#define uSoftness (featherSoftness)
#define uGamma (gamma)
#define uMattePreview (mattePreview)

          // 0-1 feather amount from top
       // 0-1 feather amount from bottom
         // 0-1 feather amount from left
        // 0-1 feather amount from right
     // 0-1 overall softness modifier
// Hero-rewrite params
        // 0.2-3.0 — falloff curve (1.0 = linear, <1 = sharper, >1 = softer)
 // 0=normal, 1=matte preview (alpha as red overlay)

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);

  float alpha = 1.0;

  if (uTop > 0.0)    alpha *= smoothstep(1.0, 1.0 - uTop, vUv.y);
  if (uBottom > 0.0) alpha *= smoothstep(0.0, uBottom, vUv.y);
  if (uLeft > 0.0)   alpha *= smoothstep(0.0, uLeft, vUv.x);
  if (uRight > 0.0)  alpha *= smoothstep(1.0, 1.0 - uRight, vUv.x);

  // Apply overall softness modifier (legacy behavior).
  alpha = pow(alpha, 1.0 / max(uSoftness + 0.5, 0.1));

  // Gamma-aware falloff. Projector edge-blending is GAMMA-space, not
  // linear. uGamma=2.2 produces the curve real projectors blend with
  // — falls off slowly in the bright zone then quickly at the edge.
  // uGamma=1 = current behavior; <1 sharpens for hard masks.
  alpha = pow(alpha, max(uGamma, 0.0001));

  // Matte preview: bypass the image and render alpha as a translucent
  // red overlay so the user can SEE the feather shape they're
  // dialling in. Same trick After Effects uses for shape masks.
  if (uMattePreview > 0.5) {
    vec3 matteR = vec3(1.0, 0.0, 0.0);
    vec3 inv = vec3(1.0) - matteR;
    // Show the feather as red opacity, opaque-black elsewhere.
    gl_FragColor = vec4(mix(vec3(0.0), matteR, 1.0 - alpha), 1.0);
    return;
  }

  gl_FragColor = vec4(texColor.rgb, texColor.a * alpha);
}
