/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Animated particle weather — rain drops or snowflakes. Adapted from Ghost Arcade's Rain / Snow effect.",
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
            "NAME": "typeValue",
            "LABEL": "Type",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "densityValue",
            "LABEL": "Density",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
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
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 20.0,
            "DEFAULT": 10.0
        },
        {
            "NAME": "sizeValue",
            "LABEL": "Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "fogAmount",
            "LABEL": "Fog Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
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
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (rainFogSnowHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uType float(floor(typeValue + 0.5))
#define uDensity (densityValue)
#define uSpeed (speed)
#define uAngle (angle)
#define uSize (sizeValue)
#define uFogAmount (fogAmount)
#define uColorR (colorR)
#define uColorG (colorG)
#define uColorB (colorB)

          // 0=rain, 1=snow, 2=mist, 3=embers
       // 0-1
         // 0-3
         // -45..45 degrees wind
          // 0.5-3 particle size
     // 0-1 fog wash

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  int type = int(uType + 0.5);

  vec2 wind = vec2(sin(radians(uAngle)), -cos(radians(uAngle))); // base downward
  if (type == 2) wind *= 0.3; // mist drifts
  if (type == 3) wind *= -0.6; // embers rise

  float t = uTime * uSpeed;
  float scale = (type == 1) ? 80.0 : (type == 2) ? 30.0 : (type == 3) ? 90.0 : 120.0;
  scale *= 1.0 / max(0.5, uSize);

  vec2 p = vUv * vec2(uResolution.x / uResolution.y, 1.0) * scale;
  vec2 cellId = floor(p);
  float lifeTime = hash21(cellId) * 10.0;
  float phase = mod(t * 0.5 + lifeTime, 1.0);

  // Particle position within cell, drifted by wind
  vec2 cellUv = fract(p) - 0.5;
  cellUv -= wind * phase * 1.5;
  cellUv = vec2(cellUv.x, fract(cellUv.y + 0.5) - 0.5);

  float d = length(cellUv);
  float particle = 0.0;

  if (type == 0) {
    // Rain — vertical streak
    float streak = smoothstep(0.05, 0.0, abs(cellUv.x)) * smoothstep(0.5, 0.0, abs(cellUv.y));
    particle = streak;
  } else if (type == 1) {
    // Snow — soft circle
    particle = smoothstep(0.15, 0.0, d);
  } else if (type == 2) {
    // Mist — large soft puff
    particle = smoothstep(0.3, 0.0, d) * 0.5;
  } else {
    // Embers — bright dot with glow
    particle = smoothstep(0.05, 0.0, d) + smoothstep(0.2, 0.05, d) * 0.3;
  }

  // Spawn probability gated by density
  float spawn = step(1.0 - uDensity, hash21(cellId + 17.0));
  particle *= spawn;

  vec3 partColor = vec3(uColorR, uColorG, uColorB);
  vec3 result = src.rgb + partColor * particle;

  // Fog wash
  if (uFogAmount > 0.001) {
    result = mix(result, partColor * 0.5, uFogAmount * 0.4);
  }
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), src.a);
}
