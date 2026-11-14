/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Unsharp mask sharpening. Adapted from Ghost Arcade's Sharpen effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Blur & Focus",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "sharpenAmount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
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
            "NAME": "radiusValue",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 4.0,
            "DEFAULT": 2.0
        },
        {
            "NAME": "edgeProtect",
            "LABEL": "Edge Protect",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "clarity",
            "LABEL": "Clarity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (sharpenHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (sharpenAmount)
#define uMode float(floor(modeValue + 0.5))
#define uRadius (radiusValue)
#define uEdgeProtect (edgeProtect)
#define uClarity (clarity)

        // 0-3 sharpen strength
          // 0=laplacian, 1=unsharp mask
        // 1-8 unsharp radius
   // 0-1 limit sharpening on flat areas
       // 0-1 mid-tone contrast pop

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec2 texel = 1.0 / uResolution;
  vec4 center = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 result = center.rgb;

  int mode = int(uMode + 0.5);

  if (mode == 0) {
    // Laplacian sharpen
    vec4 left   = IMG_NORM_PIXEL(inputImage, vUv - vec2(texel.x, 0.0));
    vec4 right  = IMG_NORM_PIXEL(inputImage, vUv + vec2(texel.x, 0.0));
    vec4 top    = IMG_NORM_PIXEL(inputImage, vUv + vec2(0.0, texel.y));
    vec4 bottom = IMG_NORM_PIXEL(inputImage, vUv - vec2(0.0, texel.y));
    vec3 avg = (left.rgb + right.rgb + top.rgb + bottom.rgb) * 0.25;
    vec3 highFreq = center.rgb - avg;
    // Edge protect: fade sharpen on flat regions
    float edgeAmp = 1.0;
    if (uEdgeProtect > 0.001) {
      float edgeMag = length(highFreq);
      edgeAmp = smoothstep(0.0, uEdgeProtect * 0.2, edgeMag);
    }
    result = center.rgb + highFreq * uAmount * edgeAmp;
  } else {
    // Unsharp mask: blur, subtract from original, add scaled back
    float r = max(1.0, uRadius);
    vec3 blurAcc = vec3(0.0);
    float wsum = 0.0;
    for (int y = -4; y <= 4; y++) {
      for (int x = -4; x <= 4; x++) {
        if (abs(x) + abs(y) > 4) continue;
        vec2 off = vec2(float(x), float(y)) * texel * (r * 0.5);
        float w = exp(-(float(x*x + y*y)) / (2.0 * r * r));
        blurAcc += IMG_NORM_PIXEL(inputImage, vUv + off).rgb * w;
        wsum += w;
      }
    }
    vec3 blurred = blurAcc / wsum;
    vec3 mask = center.rgb - blurred;
    float edgeAmp = 1.0;
    if (uEdgeProtect > 0.001) {
      float edgeMag = length(mask);
      edgeAmp = smoothstep(0.0, uEdgeProtect * 0.2, edgeMag);
    }
    result = center.rgb + mask * uAmount * edgeAmp;
  }

  // Clarity: mid-tone contrast pop
  if (uClarity > 0.001) {
    float lum = luma(result);
    float midMask = 4.0 * lum * (1.0 - lum); // peaks at lum=0.5
    vec3 popped = mix(vec3(0.5), result, 1.0 + uClarity * 0.6);
    result = mix(result, popped, midMask * uClarity);
  }

  gl_FragColor = vec4(clamp(result, 0.0, 1.0), center.a);
}
