/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Beat-triggered bloom + radial shockwave + chroma split + strobe. Reads audio for kick-driven pulse. Adapted from Ghost Arcade's Audio Shock Bloom effect.",
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
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "bloomThreshold",
            "LABEL": "Bloom Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "bloomRadius",
            "LABEL": "Bloom Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
        },
        {
            "NAME": "shockSpeed",
            "LABEL": "Shock Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.6,
            "DEFAULT": 0.8
        },
        {
            "NAME": "shockAmplitude",
            "LABEL": "Shock Amplitude",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.05
        },
        {
            "NAME": "chromaSplit",
            "LABEL": "Chroma Split",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "strobeAmount",
            "LABEL": "Strobe Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.9,
            "DEFAULT": 0.95
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
        }
    ]
}*/

// Ported from Ghost Arcade (audioShockBloomHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (intensity)
#define uBloomThreshold (bloomThreshold)
#define uBloomRadius (bloomRadius)
#define uShockSpeed (shockSpeed)
#define uShockAmplitude (shockAmplitude)
#define uChromaSplit (chromaSplit)
#define uStrobeAmount (strobeAmount)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uAudio 0.0
#define uAudioGate 0.3

     // 0-2
// 0-1
   // 1-30
    // 0.1-3
// 0-0.2
   // 0-1
  // 0-1




     // 0-1 minimum audio to trigger

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  float audio = max(0.0, uAudio - uAudioGate) * (1.0 / max(0.01, 1.0 - uAudioGate));
  float kick = audio * uIntensity;

  // Shockwave displacement (driven by audio)
  vec2 d = vUv - 0.5;
  float dist = length(d);
  float ringR = mod(uTime * uShockSpeed * 0.5, 1.4) - 0.2;
  float band = smoothstep(0.06, 0.0, abs(dist - ringR));
  vec2 dir = normalize(d + 1e-6);
  vec2 shockOff = dir * band * uShockAmplitude * (0.4 + kick);

  // Chroma split (kick-driven)
  vec3 col;
  float cs = uChromaSplit * (0.4 + kick * 0.8);
  if (cs > 0.001) {
    col.r = IMG_NORM_PIXEL(inputImage, vUv + dir * cs * 0.025 + shockOff).r;
    col.g = IMG_NORM_PIXEL(inputImage, vUv + shockOff).g;
    col.b = IMG_NORM_PIXEL(inputImage, vUv - dir * cs * 0.025 + shockOff).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, vUv + shockOff).rgb;
  }

  // Bloom (highlight blur)
  vec2 texel = 1.0 / uResolution;
  vec3 bloom = vec3(0.0);
  float wsum = 0.0;
  float br = uBloomRadius * (0.5 + kick * 1.5);
  for (int y = -3; y <= 3; y++) {
    for (int x = -3; x <= 3; x++) {
      if (abs(x) + abs(y) > 4) continue;
      vec2 off = vec2(float(x), float(y)) * texel * br * 0.4;
      vec3 s = IMG_NORM_PIXEL(inputImage, vUv + off).rgb;
      float gate = smoothstep(uBloomThreshold, uBloomThreshold + 0.15, luma(s));
      float w = exp(-(float(x*x + y*y)) / 8.0);
      bloom += s * gate * w;
      wsum += w;
    }
  }
  bloom = (wsum > 0.001) ? bloom / wsum : vec3(0.0);
  bloom *= vec3(uTintR, uTintG, uTintB) * (1.0 + kick * 2.0);

  // Strobe pulse
  float strobe = 1.0 + uStrobeAmount * kick * 1.2;
  col *= strobe;

  vec3 result = 1.0 - (1.0 - col) * (1.0 - bloom);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
