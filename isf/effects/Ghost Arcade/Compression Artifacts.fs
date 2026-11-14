/*{
    "ISFVSN": "2",
    "DESCRIPTION": "JPEG-style block compression artifacts. Adapted from Ghost Arcade's Compression Artifacts effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Stylize",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "blockSize",
            "LABEL": "Block Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 16.0,
            "DEFAULT": 8.0
        },
        {
            "NAME": "quality",
            "LABEL": "Quality",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "chromaSubsample",
            "LABEL": "Chroma Subsample",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "blockNoise",
            "LABEL": "Block Noise",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
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
        }
    ]
}*/

// Ported from Ghost Arcade (compressionArtifactsHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uBlockSize (blockSize)
#define uQuality (quality)
#define uChromaSubsample (chromaSubsample)
#define uBlockNoise (blockNoise)
#define uMode float(floor(modeValue + 0.5))
#define uMix (mixValue)

     // 4-32
       // 0-1 (low=more artifacts)
 // 0-1
    // 0-1 random per-block jitter
          // 0=DCT-style block, 1=hard 8x8, 2=color banding
           // 0-1

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

vec3 rgb2ycbcr(vec3 c) {
  return vec3(
     0.299 * c.r + 0.587 * c.g + 0.114 * c.b,
    -0.169 * c.r - 0.331 * c.g + 0.5   * c.b + 0.5,
     0.5   * c.r - 0.419 * c.g - 0.081 * c.b + 0.5
  );
}
vec3 ycbcr2rgb(vec3 c) {
  float y = c.x; float cb = c.y - 0.5; float cr = c.z - 0.5;
  return vec3(y + 1.402 * cr, y - 0.344 * cb - 0.714 * cr, y + 1.772 * cb);
}

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uMix < 0.001) { gl_FragColor = src; return; }

  int mode = int(uMode + 0.5);
  float bs = max(2.0, uBlockSize);
  vec2 px = vUv * uResolution;

  // Find block center
  vec2 blockId = floor(px / bs);
  vec2 blockCenter = (blockId + 0.5) * bs / uResolution;

  // Sample block average (proxy for low-frequency component)
  vec3 avg = vec3(0.0);
  int s = 0;
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 4; x++) {
      vec2 sp = (blockId * bs + vec2(float(x), float(y)) * bs * 0.25) / uResolution;
      avg += IMG_NORM_PIXEL(inputImage, sp).rgb;
      s++;
    }
  }
  avg /= float(s);

  vec3 result;
  if (mode == 0 || mode == 1) {
    // Quantize per block
    float qStep = mix(0.01, 0.2, 1.0 - uQuality);
    vec3 quant = floor(src.rgb / qStep) * qStep;
    // Mix block average with quantized
    float blockBias = (mode == 1) ? 0.65 : 0.45;
    result = mix(quant, avg, blockBias * (1.0 - uQuality));

    // Block-noise jitter
    if (uBlockNoise > 0.001) {
      float bn = (hash21(blockId) - 0.5) * uBlockNoise * 0.15;
      result += vec3(bn);
    }
  } else {
    // Color banding (luma-preserving bit reduction in chroma)
    vec3 ycc = rgb2ycbcr(src.rgb);
    float yStep = mix(0.005, 0.05, 1.0 - uQuality);
    ycc.x = floor(ycc.x / yStep) * yStep;
    float cStep = mix(0.02, 0.2, 1.0 - uQuality);
    ycc.yz = floor(ycc.yz / cStep) * cStep;
    result = ycbcr2rgb(ycc);
  }

  // Chroma subsample
  if (uChromaSubsample > 0.001) {
    vec3 subYcc = rgb2ycbcr(avg);
    vec3 hereYcc = rgb2ycbcr(result);
    hereYcc.yz = mix(hereYcc.yz, subYcc.yz, uChromaSubsample);
    result = ycbcr2rgb(hereYcc);
  }

  gl_FragColor = vec4(mix(src.rgb, clamp(result, 0.0, 1.0), uMix), src.a);
}
