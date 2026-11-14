/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Miniature/diorama focus band effect. Adapted from Ghost Arcade's Tilt Shift effect.",
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
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "focusY",
            "LABEL": "Focus Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "focusX",
            "LABEL": "Focus X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "focusBand",
            "LABEL": "Focus Band",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "falloffValue",
            "LABEL": "Falloff",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "maxBlur",
            "LABEL": "Max Blur",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "saturationValue",
            "LABEL": "Saturation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.4,
            "DEFAULT": 1.2
        }
    ]
}*/

// Ported from Ghost Arcade (tiltShiftHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(modeValue + 0.5))
#define uFocusY (focusY)
#define uFocusX (focusX)
#define uFocusBand (focusBand)
#define uFalloff (falloffValue)
#define uMaxBlur (maxBlur)
#define uAngle (angle)
#define uSaturation (saturationValue)

          // 0=horizontal, 1=vertical, 2=radial, 3=linear gradient
        // 0-1 focus center
        // 0-1 (used by radial)
     // 0-1 sharp band width
       // 0-1 transition softness
       // 0-1 max blur amount
         // 0-360 (linear gradient direction)
    // 0-2 saturation in defocused area (miniature look)

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 sampleBlur(vec2 uv, float r) {
  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  vec2 texel = 1.0 / uResolution;
  for (int y = -3; y <= 3; y++) {
    for (int x = -3; x <= 3; x++) {
      vec2 off = vec2(float(x), float(y)) * texel * r;
      float w = exp(-(float(x*x + y*y)) / 8.0);
      acc += IMG_NORM_PIXEL(inputImage, uv + off).rgb * w;
      wsum += w;
    }
  }
  return acc / wsum;
}

void main() {
  int mode = int(uMode + 0.5);
  float blurMask = 0.0;
  float band = max(0.001, uFocusBand);
  float falloff = max(0.001, uFalloff);

  if (mode == 0) {
    // Horizontal band
    float d = abs(vUv.y - uFocusY);
    blurMask = smoothstep(band * 0.5, band * 0.5 + falloff, d);
  } else if (mode == 1) {
    // Vertical band
    float d = abs(vUv.x - uFocusX);
    blurMask = smoothstep(band * 0.5, band * 0.5 + falloff, d);
  } else if (mode == 2) {
    // Radial spotlight focus
    vec2 d = vUv - vec2(uFocusX, uFocusY);
    float dist = length(d);
    blurMask = smoothstep(band * 0.5, band * 0.5 + falloff, dist);
  } else {
    // Linear gradient
    float ang = radians(uAngle);
    vec2 dir = vec2(cos(ang), sin(ang));
    float t = dot(vUv - vec2(uFocusX, uFocusY), dir);
    blurMask = smoothstep(-band * 0.5, band * 0.5 + falloff, abs(t));
  }

  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  float blurR = blurMask * uMaxBlur * 14.0;
  vec3 blurred = (blurR > 0.1) ? sampleBlur(vUv, blurR) : src.rgb;

  // Saturation tweak in defocused area (miniature/tilt-shift look)
  if (abs(uSaturation - 1.0) > 0.001) {
    float lum = luma(blurred);
    blurred = mix(vec3(lum), blurred, uSaturation);
  }

  vec3 result = mix(src.rgb, blurred, blurMask);
  gl_FragColor = vec4(result, src.a);
}
