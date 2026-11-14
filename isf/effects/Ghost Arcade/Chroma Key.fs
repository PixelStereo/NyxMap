/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Green/blue screen color removal. Adapted from Ghost Arcade's Chroma Key effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Keying",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "keyR",
            "LABEL": "Key R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "keyG",
            "LABEL": "Key G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "keyB",
            "LABEL": "Key B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "tolerance",
            "LABEL": "Tolerance",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.25
        },
        {
            "NAME": "softness",
            "LABEL": "Softness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.15
        },
        {
            "NAME": "spillSuppress",
            "LABEL": "Spill Suppress",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "matteValue",
            "LABEL": "Matte",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
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

// Ported from Ghost Arcade (chromaKeyHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uKeyR (keyR)
#define uKeyG (keyG)
#define uKeyB (keyB)
#define uTolerance (tolerance)
#define uSoftness (softness)
#define uSpillSuppress (spillSuppress)
#define uMatte (matteValue)
#define uMode float(floor(modeValue + 0.5))

          // 0-1 key colour (default green)


     // 0-1 hue band width
      // 0-1 edge feather
 // 0-1 reduce key colour on subject
         // 0=show matte (1-bit), 0=keyed result
          // 0=hue distance, 1=YCbCr, 2=RGB distance

vec3 rgb2ycbcr(vec3 c) {
  float y  =  0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
  float cb = -0.169 * c.r - 0.331 * c.g + 0.5   * c.b;
  float cr =  0.5   * c.r - 0.419 * c.g - 0.081 * c.b;
  return vec3(y, cb, cr);
}

vec3 rgb2hsv(vec3 c) {
  vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10;
  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

void main() {
  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec3 key = vec3(uKeyR, uKeyG, uKeyB);
  int mode = int(uMode + 0.5);

  float dist;
  if (mode == 0) {
    // Hue distance
    vec3 hsvSrc = rgb2hsv(src);
    vec3 hsvKey = rgb2hsv(key);
    float hd = abs(hsvSrc.x - hsvKey.x);
    hd = min(hd, 1.0 - hd);
    dist = hd * 2.0 + (1.0 - hsvSrc.y) * 0.3;
  } else if (mode == 1) {
    // YCbCr (chroma plane)
    vec3 yc = rgb2ycbcr(src);
    vec3 yk = rgb2ycbcr(key);
    dist = length(yc.yz - yk.yz) * 2.0;
  } else {
    // RGB distance
    dist = length(src - key);
  }

  float matte = smoothstep(uTolerance, uTolerance + uSoftness + 0.001, dist);

  // Spill suppression
  vec3 result = src;
  if (uSpillSuppress > 0.001) {
    float keyMax = max(max(key.r, key.g), key.b);
    // Reduce dominant key channel
    if (key.g >= max(key.r, key.b)) {
      result.g = min(result.g, mix(result.g, (result.r + result.b) * 0.5, uSpillSuppress * (1.0 - matte)));
    } else if (key.r >= max(key.g, key.b)) {
      result.r = min(result.r, mix(result.r, (result.g + result.b) * 0.5, uSpillSuppress * (1.0 - matte)));
    } else {
      result.b = min(result.b, mix(result.b, (result.r + result.g) * 0.5, uSpillSuppress * (1.0 - matte)));
    }
  }

  if (uMatte > 0.5) {
    gl_FragColor = vec4(vec3(matte), 1.0);
  } else {
    gl_FragColor = vec4(result, matte);
  }
}
