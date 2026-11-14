/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Infrared thermal camera look. Adapted from Ghost Arcade's Thermal effect.",
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
            "NAME": "thermalIntensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "paletteValue",
            "LABEL": "Palette",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "shimmerValue",
            "LABEL": "Shimmer",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "sensorNoise",
            "LABEL": "Sensor Noise",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (thermalShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (thermalIntensity)
#define uPalette float(floor(paletteValue + 0.5))
#define uShimmer (shimmerValue)
#define uSensorNoise (sensorNoise)

   // 0-2 contrast / temperature curve sharpness
     // 0=classic, 1=ironbow, 2=arctic, 3=predator, 4=medical
     // 0-1 — heat-haze shimmer on hot pixels
 // 0-1 — animated rolling sensor banding noise

float thHash(vec2 p) {
  return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 classicThermal(float t) {
  // Blue (cold) -> Cyan -> Green -> Yellow -> Red -> White (hot)
  vec3 color;
  if (t < 0.2) {
    color = mix(vec3(0.0, 0.0, 0.5), vec3(0.0, 0.5, 1.0), t * 5.0);
  } else if (t < 0.4) {
    color = mix(vec3(0.0, 0.5, 1.0), vec3(0.0, 1.0, 0.0), (t - 0.2) * 5.0);
  } else if (t < 0.6) {
    color = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (t - 0.4) * 5.0);
  } else if (t < 0.8) {
    color = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t - 0.6) * 5.0);
  } else {
    color = mix(vec3(1.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0), (t - 0.8) * 5.0);
  }
  return color;
}

vec3 ironbowPalette(float t) {
  // Black -> Purple -> Blue -> Cyan -> Green -> Yellow -> Orange -> Red -> White
  vec3 color;
  if (t < 0.14) {
    color = mix(vec3(0.0), vec3(0.3, 0.0, 0.5), t * 7.14);
  } else if (t < 0.28) {
    color = mix(vec3(0.3, 0.0, 0.5), vec3(0.0, 0.0, 1.0), (t - 0.14) * 7.14);
  } else if (t < 0.42) {
    color = mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), (t - 0.28) * 7.14);
  } else if (t < 0.57) {
    color = mix(vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0), (t - 0.42) * 6.67);
  } else if (t < 0.71) {
    color = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (t - 0.57) * 7.14);
  } else if (t < 0.85) {
    color = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.5, 0.0), (t - 0.71) * 7.14);
  } else {
    color = mix(vec3(1.0, 0.5, 0.0), vec3(1.0, 1.0, 1.0), (t - 0.85) * 6.67);
  }
  return color;
}

vec3 arcticPalette(float t) {
  // White (cold) -> Cyan -> Blue -> Purple -> Magenta (hot)
  vec3 color;
  if (t < 0.25) {
    color = mix(vec3(1.0, 1.0, 1.0), vec3(0.5, 1.0, 1.0), t * 4.0);
  } else if (t < 0.5) {
    color = mix(vec3(0.5, 1.0, 1.0), vec3(0.0, 0.5, 1.0), (t - 0.25) * 4.0);
  } else if (t < 0.75) {
    color = mix(vec3(0.0, 0.5, 1.0), vec3(0.5, 0.0, 1.0), (t - 0.5) * 4.0);
  } else {
    color = mix(vec3(0.5, 0.0, 1.0), vec3(1.0, 0.0, 0.5), (t - 0.75) * 4.0);
  }
  return color;
}

// Predator palette — high-contrast green/yellow/red threat-detection look.
vec3 predatorPalette(float t) {
  if (t < 0.3)      return mix(vec3(0.0, 0.05, 0.0), vec3(0.0, 0.6, 0.1), t / 0.3);
  else if (t < 0.6) return mix(vec3(0.0, 0.6, 0.1), vec3(0.95, 0.85, 0.0), (t - 0.3) / 0.3);
  else if (t < 0.85)return mix(vec3(0.95, 0.85, 0.0), vec3(0.95, 0.25, 0.05), (t - 0.6) / 0.25);
  else              return mix(vec3(0.95, 0.25, 0.05), vec3(1.0, 0.0, 0.6), (t - 0.85) / 0.15);
}
// Medical palette — clean black-to-white IR for diagnostic look.
vec3 medicalPalette(float t) {
  return vec3(t);
}

void main() {
  vec2 uv = vUv;

  // Heat shimmer — hot pixels (sampled separately) drive a tiny UV
  // wobble so the image distorts where it's hot.
  if (uShimmer > 0.001) {
    vec3 sample0 = IMG_NORM_PIXEL(inputImage, uv).rgb;
    float lum0 = dot(sample0, vec3(0.299, 0.587, 0.114));
    float wobble = sin(uv.y * 60.0 + uTime * 4.0) * 0.5 + sin(uv.x * 35.0 + uTime * 3.0) * 0.5;
    uv.x += wobble * uShimmer * lum0 * 0.006;
    uv.y += wobble * uShimmer * lum0 * 0.003;
  }

  vec4 texColor = IMG_NORM_PIXEL(inputImage, uv);

  float temp = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
  temp = pow(temp, 1.0 / max(uIntensity, 0.05));

  // Sensor noise — banded horizontal rolling noise like a cheap IR sensor.
  if (uSensorNoise > 0.001) {
    float band = thHash(vec2(floor(vUv.y * uResolution.y * 0.5), floor(uTime * 8.0)));
    temp += (band - 0.5) * uSensorNoise * 0.18;
    temp = clamp(temp, 0.0, 1.0);
  }

  vec3 thermalColor;
  int paletteType = int(uPalette);
  if (paletteType == 0)      thermalColor = classicThermal(temp);
  else if (paletteType == 1) thermalColor = ironbowPalette(temp);
  else if (paletteType == 2) thermalColor = arcticPalette(temp);
  else if (paletteType == 3) thermalColor = predatorPalette(temp);
  else                       thermalColor = medicalPalette(temp);

  gl_FragColor = vec4(thermalColor, texColor.a);
}
