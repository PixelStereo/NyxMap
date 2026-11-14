/*{
    "ISFVSN": "2",
    "DESCRIPTION": "3D embossed/raised surface look. Adapted from Ghost Arcade's Emboss effect.",
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
            "NAME": "embossStrength",
            "LABEL": "Strength",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 4.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "angleValue",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 270.0,
            "DEFAULT": 135.0
        },
        {
            "NAME": "heightValue",
            "LABEL": "Height",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "highlightR",
            "LABEL": "Highlight R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "highlightG",
            "LABEL": "Highlight G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "highlightB",
            "LABEL": "Highlight B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "shadowR",
            "LABEL": "Shadow R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "shadowG",
            "LABEL": "Shadow G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "shadowB",
            "LABEL": "Shadow B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "normalMode",
            "LABEL": "Normal Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "metallicness",
            "LABEL": "Metallicness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (embossShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uStrength (embossStrength)
#define uAngle (angleValue)
#define uHeight (heightValue)
#define uHighlightR (highlightR)
#define uHighlightG (highlightG)
#define uHighlightB (highlightB)
#define uShadowR (shadowR)
#define uShadowG (shadowG)
#define uShadowB (shadowB)
#define uNormalMode float(floor(normalMode + 0.5))
#define uMetallicness (metallicness)

        // 0-2 emboss strength
           // 0-360 light direction
          // 0-1 surface height exaggeration
      // 0-1 highlight tint


         // 0-1 shadow tint


      // 0=emboss (relit), 1=normal-map preview
    // 0-1 boosts highlight reflectivity

float embossLum(vec3 c) {
  return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
  vec2 texel = 1.0 / uResolution;
  float angle = uAngle * 3.14159265 / 180.0;
  vec2 dir = vec2(cos(angle), sin(angle));

  // Build a surface normal from luma gradients in 4 directions.
  float lL = embossLum(IMG_NORM_PIXEL(inputImage, vUv - vec2(texel.x, 0.0)).rgb);
  float lR = embossLum(IMG_NORM_PIXEL(inputImage, vUv + vec2(texel.x, 0.0)).rgb);
  float lD = embossLum(IMG_NORM_PIXEL(inputImage, vUv - vec2(0.0, texel.y)).rgb);
  float lU = embossLum(IMG_NORM_PIXEL(inputImage, vUv + vec2(0.0, texel.y)).rgb);
  float dx = (lR - lL) * (1.0 + uHeight * 4.0);
  float dy = (lU - lD) * (1.0 + uHeight * 4.0);
  vec3 normal = normalize(vec3(-dx, -dy, 1.0));

  // Normal-map preview — encode normal as RGB.
  if (uNormalMode > 0.5) {
    gl_FragColor = vec4(normal * 0.5 + 0.5, IMG_NORM_PIXEL(inputImage, vUv).a);
    return;
  }

  // Light direction in 3D from uAngle (azimuth) and a fixed elevation.
  vec3 light = normalize(vec3(dir.x, dir.y, 0.5));
  float diff = max(dot(normal, light), 0.0);
  float spec = pow(diff, mix(8.0, 64.0, uMetallicness)) * uMetallicness;

  // Emboss intensity from gradient along light direction.
  float along = (lR - lL) * dir.x + (lU - lD) * dir.y;
  float emboss = clamp(along * uStrength + 0.5, 0.0, 1.0);

  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 hi = vec3(uHighlightR, uHighlightG, uHighlightB);
  vec3 lo = vec3(uShadowR, uShadowG, uShadowB);
  vec3 lit = mix(lo, hi, emboss);

  // Blend the lit surface with the source color so detail isn't lost.
  vec3 finalCol = texColor.rgb * 0.5 + lit + vec3(spec);

  gl_FragColor = vec4(clamp(finalCol, 0.0, 1.0), texColor.a);
}
