/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Animated layered perlin noise fog overlay. Adapted from Ghost Arcade's Volumetric Fog effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Atmosphere",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "density",
            "LABEL": "Density",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "scale",
            "LABEL": "Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 12.0,
            "DEFAULT": 6.0
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "heightFalloff",
            "LABEL": "Height Falloff",
            "TYPE": "float",
            "MIN": -0.6,
            "MAX": 0.6,
            "DEFAULT": -0.3
        },
        {
            "NAME": "depthSim",
            "LABEL": "Depth Sim",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "colorR",
            "LABEL": "Color R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
        },
        {
            "NAME": "colorG",
            "LABEL": "Color G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.8,
            "DEFAULT": 0.9
        },
        {
            "NAME": "colorB",
            "LABEL": "Color B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.9,
            "DEFAULT": 0.95
        },
        {
            "NAME": "turbulence",
            "LABEL": "Turbulence",
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
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (volumetricFogHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uDensity (density)
#define uScale (scale)
#define uSpeed (speed)
#define uHeightFalloff (heightFalloff)
#define uDepthSim (depthSim)
#define uColorR (colorR)
#define uColorG (colorG)
#define uColorB (colorB)
#define uTurbulence (turbulence)
#define uMode float(floor(modeValue + 0.5))

       // 0-1
         // 1-32
         // 0-2
 // -1..1 (positive=sky fog, negative=ground fog)
      // 0-1 (use luma as fake depth)



    // 0-1
          // 0=add, 1=mix, 2=subtract

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  float a = hash21(i), b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0)), d = hash21(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm(vec2 p) {
  float v = 0.0; float amp = 0.5;
  for (int i = 0; i < 4; i++) { v += vnoise(p) * amp; p *= 2.0; amp *= 0.5; }
  return v;
}
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uDensity < 0.001) { gl_FragColor = src; return; }

  vec2 p = vUv * uScale + vec2(uTime * uSpeed * 0.1, -uTime * uSpeed * 0.05);
  float fog = uTurbulence > 0.5 ? fbm(p) : vnoise(p);
  fog *= uDensity;

  // Height falloff
  float heightW = mix(1.0 - vUv.y, vUv.y, (uHeightFalloff + 1.0) * 0.5);
  fog *= heightW;

  // Depth-aware: brighter pixels = farther in fog (sim depth from luma)
  if (uDepthSim > 0.001) {
    float fakeDepth = 1.0 - luma(src.rgb);
    fog *= mix(1.0, fakeDepth, uDepthSim);
  }

  fog = clamp(fog, 0.0, 1.0);
  vec3 fogColor = vec3(uColorR, uColorG, uColorB);

  int mode = int(uMode + 0.5);
  vec3 result;
  if (mode == 0) result = src.rgb + fogColor * fog;
  else if (mode == 1) result = mix(src.rgb, fogColor, fog);
  else result = src.rgb - fogColor * fog * 0.5;

  gl_FragColor = vec4(clamp(result, 0.0, 1.0), src.a);
}
