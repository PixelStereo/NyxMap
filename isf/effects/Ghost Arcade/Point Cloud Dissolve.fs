/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Image becomes scattered dots with attract / scatter / reform animation. Square / circle / cross dot shapes. Adapted from Ghost Arcade's Point Cloud Dissolve effect.",
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
            "NAME": "dissolveValue",
            "LABEL": "Dissolve",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "dotSize",
            "LABEL": "Dot Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 4.0
        },
        {
            "NAME": "scatterRadius",
            "LABEL": "Scatter Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "attract",
            "LABEL": "Attract",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "turbulence",
            "LABEL": "Turbulence",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
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
        },
        {
            "NAME": "hueShift",
            "LABEL": "Hue Shift",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (pointCloudDissolveHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uDissolve (dissolveValue)
#define uDotSize (dotSize)
#define uScatterRadius (scatterRadius)
#define uAttract (attract)
#define uTurbulence (turbulence)
#define uMode float(floor(modeValue + 0.5))
#define uBgR (bgR)
#define uBgG (bgG)
#define uBgB (bgB)
#define uHueShift (hueShift)

      // 0-1 0=image, 1=fully scattered
       // 1-12 px
 // 0-1 max scatter distance (% of screen)
       // 0-1 swirl-toward-center
    // 0-1 random per-dot direction
          // 0=square dots, 1=circle, 2=cross



      // 0-1 cycle hue along dissolve

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec3 hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
vec3 rgb2hsv(vec3 c) {
  vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}

void main() {
  // Raw source for dissolve = 0 fallback
  vec3 srcRaw = IMG_NORM_PIXEL(inputImage, vUv).rgb;

  vec2 cell = floor(vUv * uResolution / uDotSize);
  vec2 cellOrigin = cell * uDotSize / uResolution;
  vec2 cellSize = vec2(uDotSize) / uResolution;

  // Each dot's home position is the cell origin + 0.5 of cell
  vec2 home = cellOrigin + cellSize * 0.5;

  // Sample source colour at the home position
  vec3 sampleCol = IMG_NORM_PIXEL(inputImage, home).rgb;

  // Compute dot's drifted position based on dissolve amount
  vec2 dir = normalize(vec2(hash21(cell) - 0.5, hash21(cell + 13.7) - 0.5) + 1e-6);
  if (uAttract > 0.001) {
    vec2 toCenter = (vec2(0.5) - home);
    dir = mix(dir, normalize(toCenter + 1e-6), uAttract);
  }
  if (uTurbulence > 0.001) {
    float wob = sin(uTime + hash21(cell + 71.3) * 6.28) * uTurbulence;
    dir += vec2(wob * 0.3, wob * 0.4);
    dir = normalize(dir);
  }

  vec2 dotPos = home + dir * uDissolve * uScatterRadius;

  // Determine mask of dot at this UV. Dot radius = 0.71 covers full cell at
  // dissolve=0 (length(toDot) max is 0.707 at corner). Shrinks with dissolve
  // so scattered state shows BG between dots.
  vec2 toDot = (vUv - dotPos) / cellSize;
  int mode = int(uMode + 0.5);
  float dotR = mix(0.72, 0.42, uDissolve); // shrinks as dissolve grows
  float mask = 0.0;
  if (mode == 0) {
    // Square: cover full cell at dissolve=0
    vec2 ad = abs(toDot);
    mask = step(max(ad.x, ad.y), max(0.5, dotR));
  } else if (mode == 1) {
    // Circle: smooth falloff
    mask = smoothstep(dotR + 0.05, dotR - 0.05, length(toDot));
  } else {
    // Cross
    mask = max(
      step(abs(toDot.x), dotR * 0.2) * step(abs(toDot.y), dotR),
      step(abs(toDot.y), dotR * 0.2) * step(abs(toDot.x), dotR)
    );
  }

  if (uHueShift > 0.001) {
    vec3 hsv = rgb2hsv(sampleCol);
    hsv.x = fract(hsv.x + uDissolve * uHueShift);
    sampleCol = hsv2rgb(hsv);
  }

  vec3 bg = vec3(uBgR, uBgG, uBgB);
  vec3 dotResult = mix(bg, sampleCol, mask);
  // Crossfade with raw source so dissolve = 0 looks unchanged
  vec3 result = mix(srcRaw, dotResult, smoothstep(0.0, 0.05, uDissolve));
  gl_FragColor = vec4(result, 1.0);
}
