/*{
    "ISFVSN": "2",
    "DESCRIPTION": "CRT monitor — scanlines, RGB mask, curvature. Adapted from Ghost Arcade's CRT effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Generate & Texture",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "scanlines",
            "LABEL": "Scanlines",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "scanCount",
            "LABEL": "Scan Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 960.0,
            "DEFAULT": 480.0
        },
        {
            "NAME": "maskValue",
            "LABEL": "Mask",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "maskType",
            "LABEL": "Mask Type",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "curvatureValue",
            "LABEL": "Curvature",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "vignette",
            "LABEL": "Vignette",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "glowValue",
            "LABEL": "Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "rollingBar",
            "LABEL": "Rolling Bar",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "chromatic",
            "LABEL": "Chromatic",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        }
    ]
}*/

// Ported from Ghost Arcade (crtHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uScanlines (scanlines)
#define uScanCount (scanCount)
#define uMask (maskValue)
#define uMaskType float(floor(maskType + 0.5))
#define uCurvature (curvatureValue)
#define uVignette (vignette)
#define uGlow (glowValue)
#define uRollingBar (rollingBar)
#define uChromatic (chromatic)

     // 0-1 scanline strength
     // 100-1200 scanline count
          // 0-1 phosphor mask strength
      // 0=Trinitron stripe, 1=Aperture grille, 2=Shadow mask
     // 0-1 barrel curvature
      // 0-1 corner darken
          // 0-1 phosphor glow bleed
    // 0-1 vertical roll
     // 0-1 lens fringing

vec2 curveUv(vec2 uv, float k) {
  uv = uv * 2.0 - 1.0;
  vec2 offset = abs(uv.yx) / vec2(6.0, 4.0);
  uv = uv + uv * offset * offset * k;
  return uv * 0.5 + 0.5;
}

void main() {
  vec2 uv = uCurvature > 0.001 ? curveUv(vUv, uCurvature) : vUv;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  // Chromatic aberration on RGB channels
  vec3 col;
  if (uChromatic > 0.001) {
    vec2 cd = (uv - 0.5) * uChromatic * 0.01;
    col.r = IMG_NORM_PIXEL(inputImage, uv + cd).r;
    col.g = IMG_NORM_PIXEL(inputImage, uv).g;
    col.b = IMG_NORM_PIXEL(inputImage, uv - cd).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, uv).rgb;
  }

  // Phosphor mask
  if (uMask > 0.001) {
    int mtype = int(uMaskType + 0.5);
    vec2 px = uv * uResolution;
    vec3 maskCol = vec3(1.0);
    if (mtype == 0) {
      // Trinitron vertical stripes (3-pixel R/G/B repeat)
      float stripe = mod(px.x, 3.0);
      if (stripe < 1.0) maskCol = vec3(1.4, 0.6, 0.6);
      else if (stripe < 2.0) maskCol = vec3(0.6, 1.4, 0.6);
      else maskCol = vec3(0.6, 0.6, 1.4);
    } else if (mtype == 1) {
      // Aperture grille (vertical stripes + horizontal damping wires)
      float stripe = mod(px.x, 3.0);
      if (stripe < 1.0) maskCol = vec3(1.5, 0.5, 0.5);
      else if (stripe < 2.0) maskCol = vec3(0.5, 1.5, 0.5);
      else maskCol = vec3(0.5, 0.5, 1.5);
      float wire = step(0.95, mod(px.y * 0.005, 1.0));
      maskCol *= 1.0 - wire * 0.3;
    } else {
      // Shadow mask (RGB triads on diamond)
      float u3 = mod(px.x, 6.0);
      float v3 = mod(px.y, 2.0);
      if (v3 < 1.0) {
        if (u3 < 2.0) maskCol = vec3(1.5, 0.5, 0.5);
        else if (u3 < 4.0) maskCol = vec3(0.5, 1.5, 0.5);
        else maskCol = vec3(0.5, 0.5, 1.5);
      } else {
        if (u3 < 1.0 || u3 >= 5.0) maskCol = vec3(0.5, 0.5, 1.5);
        else if (u3 < 3.0) maskCol = vec3(1.5, 0.5, 0.5);
        else maskCol = vec3(0.5, 1.5, 0.5);
      }
    }
    col = mix(col, col * maskCol, uMask);
  }

  // Scanlines
  if (uScanlines > 0.001) {
    float sl = sin(uv.y * uScanCount * 3.14159) * 0.5 + 0.5;
    col *= mix(1.0, sl, uScanlines);
  }

  // Glow (sample_ surrounding pixels weighted)
  if (uGlow > 0.001) {
    vec2 texel = 1.0 / uResolution;
    vec3 g = IMG_NORM_PIXEL(inputImage, uv + vec2( texel.x,  0.0)).rgb
           + IMG_NORM_PIXEL(inputImage, uv + vec2(-texel.x,  0.0)).rgb
           + IMG_NORM_PIXEL(inputImage, uv + vec2( 0.0,  texel.y)).rgb
           + IMG_NORM_PIXEL(inputImage, uv + vec2( 0.0, -texel.y)).rgb;
    col += g * uGlow * 0.05;
  }

  // Rolling sync bar
  if (uRollingBar > 0.001) {
    float bar = sin(uv.y * 6.0 - uTime * 1.5);
    bar = smoothstep(0.7, 1.0, bar);
    col += bar * uRollingBar * 0.15;
  }

  // Vignette
  if (uVignette > 0.001) {
    vec2 vc = uv - 0.5;
    float v = 1.0 - dot(vc, vc) * uVignette * 1.4;
    col *= clamp(v, 0.0, 1.0);
  }

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
