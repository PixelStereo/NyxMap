/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Dissolve image into lit 3D particles with depth and scatter. Adapted from Ghost Arcade's 3D Particles effect.",
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
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "density",
            "LABEL": "Density",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "size",
            "LABEL": "Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "twinkle",
            "LABEL": "Twinkle",
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
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "colorG",
            "LABEL": "Color G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "colorB",
            "LABEL": "Color B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.8,
            "DEFAULT": 0.9
        },
        {
            "NAME": "blend",
            "LABEL": "Blend",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (particleOverlayHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(modeValue + 0.5))
#define uDensity (density)
#define uSize (size)
#define uSpeed (speed)
#define uTwinkle (twinkle)
#define uColorR (colorR)
#define uColorG (colorG)
#define uColorB (colorB)
#define uBlend (blend)

          // 0=stars, 1=bokeh, 2=sparkles, 3=fireflies, 4=dust
       // 0-1
          // 0.5-4
         // 0-3
       // 0-1



         // 0=add, 1=screen

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uDensity < 0.001) { gl_FragColor = src; return; }

  int mode = int(uMode + 0.5);
  float scale = (mode == 0) ? 60.0 : (mode == 1) ? 25.0 : (mode == 2) ? 80.0 : (mode == 3) ? 35.0 : 100.0;
  scale *= 1.0 / max(0.5, uSize);
  vec2 p = vUv * vec2(uResolution.x / uResolution.y, 1.0) * scale;
  vec2 cellId = floor(p);
  vec2 cellUv = fract(p) - 0.5;

  float spawn = step(1.0 - uDensity, hash21(cellId));
  if (spawn < 0.5) { gl_FragColor = src; return; }

  // Drift
  float t = uTime * uSpeed;
  vec2 drift = vec2(
    hash21(cellId + 7.3) - 0.5,
    hash21(cellId + 13.7) - 0.5
  ) * t * 0.05;
  cellUv -= drift;

  float d = length(cellUv);
  float particle = 0.0;
  if (mode == 0) {
    // Stars — small bright dot + cross flare
    particle = smoothstep(0.05, 0.0, d);
    particle += smoothstep(0.02, 0.0, abs(cellUv.x)) * smoothstep(0.3, 0.0, abs(cellUv.y)) * 0.5;
    particle += smoothstep(0.02, 0.0, abs(cellUv.y)) * smoothstep(0.3, 0.0, abs(cellUv.x)) * 0.5;
  } else if (mode == 1) {
    // Bokeh — soft disc
    particle = smoothstep(0.4, 0.1, d) * 0.6 + smoothstep(0.45, 0.4, d) * 0.4;
  } else if (mode == 2) {
    // Sparkles — bright pinpoint
    particle = smoothstep(0.04, 0.0, d) * 1.5;
  } else if (mode == 3) {
    // Fireflies — flickering soft glow
    particle = smoothstep(0.15, 0.0, d) * 0.8;
  } else {
    // Dust — many tiny specks
    particle = smoothstep(0.025, 0.0, d) * 0.6;
  }

  // Twinkle
  if (uTwinkle > 0.001) {
    float blink = sin(t * 4.0 + hash21(cellId) * 6.28) * 0.5 + 0.5;
    particle *= mix(1.0, blink, uTwinkle);
  }

  vec3 partColor = vec3(uColorR, uColorG, uColorB);
  int blendMode = int(uBlend + 0.5);
  vec3 result;
  if (blendMode == 0) {
    result = src.rgb + partColor * particle;
  } else {
    result = 1.0 - (1.0 - src.rgb) * (1.0 - partColor * particle);
  }
  gl_FragColor = vec4(result, src.a);
}
