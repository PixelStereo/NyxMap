/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Gray-Scott pattern simulation grown over the source. Spots / stripes / mitosis / coral patterns, masked by luma. Adapted from Ghost Arcade's Reaction Diffusion effect.",
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
            "NAME": "feedRate",
            "LABEL": "Feed Rate",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.055
        },
        {
            "NAME": "killRate",
            "LABEL": "Kill Rate",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.062
        },
        {
            "NAME": "diffusionA",
            "LABEL": "Diffusion A",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "diffusionB",
            "LABEL": "Diffusion B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "patternScale",
            "LABEL": "Pattern Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "lumaMask",
            "LABEL": "Luma Mask",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
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
            "NAME": "colorR",
            "LABEL": "Color R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "colorG",
            "LABEL": "Color G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
        },
        {
            "NAME": "colorB",
            "LABEL": "Color B",
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
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "reseed",
            "LABEL": "Reseed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        }
    ],
    "PASSES": [
        {
            "TARGET": "feedbackBuffer",
            "PERSISTENT": true,
            "FLOAT": true
        },
        {}
    ]
}*/

// Ported from Ghost Arcade (reactionDiffusionHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uFeedRate (feedRate)
#define uKillRate (killRate)
#define uDiffusionA (diffusionA)
#define uDiffusionB (diffusionB)
#define uPatternScale (patternScale)
#define uLumaMask (lumaMask)
#define uMode float(floor(modeValue + 0.5))
#define uColorR (colorR)
#define uColorG (colorG)
#define uColorB (colorB)
#define uMix (mixValue)
#define uReseed (reseed)
#define uHasFeedback (FRAMEINDEX > 0 ? 1.0 : 0.0)

      // 0-0.1 (typical 0.055)
      // 0-0.1 (typical 0.062)
    // 0.5-1.5
    // 0.2-1
  // 0.5-4 size of grid in pattern
      // 0-1 how much source luma drives feed
          // 0=spots, 1=stripes, 2=mitosis, 3=coral




        // 0-1 sprinkle chemical B at high-luma pixels

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void ghostArcadeEffect() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec2 texel = 1.0 / uResolution * uPatternScale;

  vec2 ab;
  if (uHasFeedback < 0.5) {
    // Initial seed: small dot at center
    float d = length(vUv - 0.5);
    ab = vec2(1.0, smoothstep(0.05, 0.0, d));
  } else {
    vec2 prev = IMG_NORM_PIXEL(feedbackBuffer, vUv).rg;
    // 5-tap Laplacian
    vec2 lap = vec2(0.0);
    lap += IMG_NORM_PIXEL(feedbackBuffer, vUv + texel * vec2(-1.0,  0.0)).rg * 0.2;
    lap += IMG_NORM_PIXEL(feedbackBuffer, vUv + texel * vec2( 1.0,  0.0)).rg * 0.2;
    lap += IMG_NORM_PIXEL(feedbackBuffer, vUv + texel * vec2( 0.0, -1.0)).rg * 0.2;
    lap += IMG_NORM_PIXEL(feedbackBuffer, vUv + texel * vec2( 0.0,  1.0)).rg * 0.2;
    lap += IMG_NORM_PIXEL(feedbackBuffer, vUv + texel * vec2(-1.0, -1.0)).rg * 0.05;
    lap += IMG_NORM_PIXEL(feedbackBuffer, vUv + texel * vec2( 1.0, -1.0)).rg * 0.05;
    lap += IMG_NORM_PIXEL(feedbackBuffer, vUv + texel * vec2(-1.0,  1.0)).rg * 0.05;
    lap += IMG_NORM_PIXEL(feedbackBuffer, vUv + texel * vec2( 1.0,  1.0)).rg * 0.05;
    lap -= prev;

    // Gray-Scott
    int mode = int(uMode + 0.5);
    float feed = uFeedRate;
    float kill = uKillRate;
    if (mode == 1) { feed = 0.039; kill = 0.058; } // stripes
    else if (mode == 2) { feed = 0.0367; kill = 0.0649; } // mitosis
    else if (mode == 3) { feed = 0.0545; kill = 0.062; } // coral

    // Luma modulates feed rate (image content drives growth)
    float srcL = luma(src);
    feed = mix(feed, feed * (0.6 + srcL * 1.0), uLumaMask);

    float a = prev.r, b = prev.g;
    float reaction = a * b * b;
    float dt = 1.0;
    float newA = a + (uDiffusionA * lap.r - reaction + feed * (1.0 - a)) * dt;
    float newB = b + (uDiffusionB * lap.g + reaction - (kill + feed) * b) * dt;
    ab = vec2(clamp(newA, 0.0, 1.0), clamp(newB, 0.0, 1.0));

    // Reseed B from bright source pixels
    if (uReseed > 0.001 && srcL > 0.7) {
      ab.g = max(ab.g, srcL * uReseed);
    }
  }

  // Visualise: B channel as colour overlay
  vec3 patColor = vec3(uColorR, uColorG, uColorB) * ab.g;
  vec3 disp = mix(src, src + patColor, uMix);
  // Encode chemical state in r,g; visible color in b
  gl_FragColor = vec4(ab, luma(disp), 1.0);
}

void main() {
    if (PASSINDEX == 0) ghostArcadeEffect();
    else gl_FragColor = IMG_NORM_PIXEL(feedbackBuffer, isf_FragNormCoord);
}
