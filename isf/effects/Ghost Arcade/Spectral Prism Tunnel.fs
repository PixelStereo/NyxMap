/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Refracts the image into recursive rainbow tunnel slices with depth fade and rotation. Adapted from Ghost Arcade's Spectral Prism Tunnel effect.",
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
            "NAME": "tunnelDepth",
            "LABEL": "Tunnel Depth",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.5
        },
        {
            "NAME": "prismSpread",
            "LABEL": "Prism Spread",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "rotation",
            "LABEL": "Rotation",
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
            "NAME": "slicesValue",
            "LABEL": "Slices",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
        },
        {
            "NAME": "fade",
            "LABEL": "Fade",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        }
    ]
}*/

// Ported from Ghost Arcade (spectralPrismTunnelHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uTunnelDepth (tunnelDepth)
#define uPrismSpread (prismSpread)
#define uRotation (rotation)
#define uSpeed (speed)
#define uSlices (slicesValue)
#define uFade (fade)

   // 0.5-3
   // 0-2 chromatic per-slice
      // 0-3
         // 0-3
        // 4-32 number of recursive slices
          // 0-1 darken with depth

vec3 hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
  vec2 d = vUv - 0.5;
  d.x *= uResolution.x / uResolution.y;
  float r = length(d);
  float a = atan(d.y, d.x);
  int slices = int(clamp(uSlices, 4.0, 32.0));
  float t = uTime * uSpeed;

  vec3 acc = vec3(0.0);
  float wsum = 0.0;
  for (int i = 0; i < 32; i++) {
    if (i >= slices) break;
    float fi = float(i) / float(slices);
    // Tunnel UV: depth shrinks r
    float depth = exp(-fi * uTunnelDepth);
    vec2 td = d / depth;
    td.x *= uResolution.y / uResolution.x;
    float ang = a + uRotation * fi + t * 0.3;
    vec2 rotD = vec2(cos(ang), sin(ang)) * length(td);
    rotD.x *= uResolution.y / uResolution.x;
    vec2 sUv = 0.5 + rotD;
    sUv = clamp(sUv, vec2(0.0), vec2(1.0));

    // Per-slice prism shift: hue offset
    vec3 sCol = IMG_NORM_PIXEL(inputImage, sUv).rgb;
    vec3 prismTint = hsv2rgb(vec3(fract(fi * uPrismSpread + t * 0.1), 1.0, 1.0));
    vec3 c = mix(sCol, sCol * prismTint, uPrismSpread * 0.5);
    float w = 1.0 - fi * uFade;
    acc += c * w;
    wsum += w;
  }
  vec3 result = acc / max(wsum, 0.0001);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
