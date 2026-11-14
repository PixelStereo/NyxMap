/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Animated CRT-style scanline overlay. Adapted from Ghost Arcade's Scanlines effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Stylize",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "scanlinesIntensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "scanlinesCount",
            "LABEL": "Count",
            "TYPE": "float",
            "MIN": 50.0,
            "MAX": 500.0,
            "DEFAULT": 200.0
        },
        {
            "NAME": "speedValue",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "phosphor",
            "LABEL": "Phosphor",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
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
            "NAME": "curvatureValue",
            "LABEL": "Curvature",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "interlaceValue",
            "LABEL": "Interlace",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (scanlinesShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (scanlinesIntensity)
#define uCount (scanlinesCount)
#define uSpeed (speedValue)
#define uPhosphor (phosphor)
#define uRollingBar (rollingBar)
#define uCurvature (curvatureValue)
#define uInterlace (interlaceValue)

   // 0-1 scanline darkening strength
       // 50-500 scanline count
       // 0-2 scanline scroll speed
    // 0-1 RGB sub-pixel mask intensity
  // 0-1 brightness bar that rolls down the screen
   // 0-1 barrel distortion (CRT bulge)
   // 0-1 interlace flicker (alternating odd/even rows)

void main() {
  vec2 uv = vUv;

  // CRT barrel curvature — push UVs outward proportional to distance²
  // from center. Sample is clamped so we don't read off-texture.
  if (uCurvature > 0.001) {
    vec2 centered = uv - 0.5;
    float r2 = dot(centered, centered);
    uv = centered * (1.0 + r2 * uCurvature * 0.4) + 0.5;
    // Black mask outside the curved tube.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
      gl_FragColor = vec4(0.0, 0.0, 0.0, IMG_NORM_PIXEL(inputImage, vUv).a);
      return;
    }
  }

  vec4 texColor = IMG_NORM_PIXEL(inputImage, uv);

  // Scanline darkening (legacy).
  float scanlinePos = uv.y * uCount + uTime * uSpeed * 50.0;
  float scanline = sin(scanlinePos * 3.14159) * 0.5 + 0.5;
  float darkness = 1.0 - uIntensity * scanline * 0.5;

  vec3 col = texColor.rgb * darkness;

  // Phosphor RGB sub-pixel mask — divides each row of pixels into RGB
  // triads, brightening each color channel only on its sub-cell.
  if (uPhosphor > 0.001) {
    float subpixel = mod(floor(uv.x * uResolution.x), 3.0);
    vec3 mask = vec3(
      subpixel < 0.5 ? 1.0 : 0.4,
      (subpixel >= 0.5 && subpixel < 1.5) ? 1.0 : 0.4,
      subpixel >= 1.5 ? 1.0 : 0.4
    );
    col *= mix(vec3(1.0), mask, uPhosphor);
  }

  // Rolling brightness bar — slow horizontal band that scrolls down.
  if (uRollingBar > 0.001) {
    float barPos = fract(uv.y - uTime * 0.15);
    float bar = smoothstep(0.0, 0.05, barPos) * smoothstep(0.15, 0.10, barPos);
    col *= 1.0 + bar * uRollingBar * 0.4;
  }

  // Interlace flicker — alternate-row brightness flicker at refresh rate.
  if (uInterlace > 0.001) {
    float row = floor(uv.y * uResolution.y);
    float frame = floor(uTime * 30.0);
    float flicker = mod(row + frame, 2.0);
    col *= mix(1.0, mix(0.85, 1.15, flicker), uInterlace);
  }

  gl_FragColor = vec4(col, texColor.a);
}
