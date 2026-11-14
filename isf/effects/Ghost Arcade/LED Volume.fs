/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Image mapped to 3D LED voxel wall with depth pulse, glow, and perspective. Square / round / hex voxels. Adapted from Ghost Arcade's LED Volume effect.",
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
            "NAME": "voxelSize",
            "LABEL": "Voxel Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 32.0,
            "DEFAULT": 16.0
        },
        {
            "NAME": "depthPulse",
            "LABEL": "Depth Pulse",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "depthSpeed",
            "LABEL": "Depth Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "posterizeValue",
            "LABEL": "Posterize",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "glow",
            "LABEL": "Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "perspectiveValue",
            "LABEL": "Perspective",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "bgR",
            "LABEL": "Bg R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "bgG",
            "LABEL": "Bg G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "bgB",
            "LABEL": "Bg B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (ledVolumeHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uVoxelSize (voxelSize)
#define uDepthPulse (depthPulse)
#define uDepthSpeed (depthSpeed)
#define uPosterize (posterizeValue)
#define uGlow (glow)
#define uPerspective (perspectiveValue)
#define uMode float(floor(modeValue + 0.5))
#define uBgR (bgR)
#define uBgG (bgG)
#define uBgB (bgB)

     // 8-32 px
    // 0-1 pulses depth
    // 0-3
     // 1-8 colour quantization
          // 0-1
   // 0-1 fake 3D push
          // 0=square, 1=round, 2=hex

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  // Voxel grid with depth pulse animation
  vec2 cell = floor(vUv * uResolution / uVoxelSize);
  vec2 cellOrigin = cell * uVoxelSize / uResolution;
  vec2 cellSize = vec2(uVoxelSize) / uResolution;
  vec2 cellUv = (vUv - cellOrigin) / cellSize - 0.5;

  // Sample center colour, posterize
  vec3 sampleCol = IMG_NORM_PIXEL(inputImage, cellOrigin + cellSize * 0.5).rgb;
  float steps = max(1.0, uPosterize);
  sampleCol = floor(sampleCol * steps + 0.5) / steps;

  // Depth pulse: brightness sized
  float depth = luma(sampleCol);
  if (uDepthPulse > 0.001) {
    depth += sin(uTime * uDepthSpeed * 2.0 + depth * 8.0) * uDepthPulse * 0.2;
  }

  // Voxel size scales with depth (perspective)
  float scale = mix(0.45, 0.45 - uPerspective * 0.3 * (1.0 - depth), 1.0);
  float r = length(cellUv);
  int mode = int(uMode + 0.5);
  float voxel;
  if (mode == 0) {
    vec2 ad = abs(cellUv);
    voxel = step(max(ad.x, ad.y), scale);
  } else if (mode == 1) {
    voxel = smoothstep(scale + 0.05, scale - 0.05, r);
  } else {
    // Hex
    vec2 ad = abs(cellUv);
    voxel = step(max(ad.x * 0.866 + ad.y * 0.5, ad.y), scale);
  }

  vec3 bg = vec3(uBgR, uBgG, uBgB);
  vec3 result = mix(bg, sampleCol, voxel);

  // Glow halo
  if (uGlow > 0.001) {
    float halo = smoothstep(scale * 1.6, scale, r);
    result += sampleCol * halo * uGlow * 0.4;
  }
  gl_FragColor = vec4(result, 1.0);
}
