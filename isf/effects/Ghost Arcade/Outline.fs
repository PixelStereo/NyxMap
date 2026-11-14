/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Glowing colored edge outlines. Adapted from Ghost Arcade's Outline effect.",
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
            "NAME": "outlineThickness",
            "LABEL": "Thickness",
            "TYPE": "float",
            "MIN": 1.0,
            "MAX": 10.0,
            "DEFAULT": 2.0
        },
        {
            "NAME": "onlyValue",
            "LABEL": "Only",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "glowValue",
            "LABEL": "Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "positionValue",
            "LABEL": "Position",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 4.0,
            "DEFAULT": 2.0
        },
        {
            "NAME": "crawlValue",
            "LABEL": "Crawl",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "glowFalloff",
            "LABEL": "Glow Falloff",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.5
        },
        {
            "NAME": "alphaAware",
            "LABEL": "Alpha Aware",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (outlineShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uThickness (outlineThickness)
#define uColor vec3(1.0, 1.0, 1.0)
#define uOnly (onlyValue)
#define uGlow (glowValue)
#define uPosition (positionValue)
#define uCrawl (crawlValue)
#define uGlowFalloff (glowFalloff)
#define uAlphaAware (alphaAware)

   // 1-10 outline thickness
        // Outline color
        // 0=overlay, 1=outline only
        // 0-1 glow amount
    // 0=outer, 1=inner, 2=both (centered on edge)
       // 0-1 — animated marching-ants crawl speed
 // 0.5-3 — falloff power (lower=tighter, higher=softer)
  // 0-1 — use source alpha as the boundary instead of luma

float outlineLum(vec3 c) {
  return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec2 texel = uThickness / uResolution;
  // Boundary metric: luma by default, alpha when alpha-aware.
  float centerVal = mix(outlineLum(texColor.rgb), texColor.a, uAlphaAware);

  float edge = 0.0;
  float insideSum = 0.0;
  float outsideSum = 0.0;
  for (int i = -1; i <= 1; i++) {
    for (int j = -1; j <= 1; j++) {
      if (i == 0 && j == 0) continue;
      vec2 off = vec2(float(i), float(j)) * texel;
      vec4 nb4 = IMG_NORM_PIXEL(inputImage, vUv + off);
      float nb = mix(outlineLum(nb4.rgb), nb4.a, uAlphaAware);
      edge += abs(nb - centerVal);
      // Inner = current pixel is brighter than neighbor, outer = darker.
      if (nb < centerVal) insideSum += (centerVal - nb);
      else                outsideSum += (nb - centerVal);
    }
  }
  edge = edge / 8.0;
  edge = smoothstep(0.05, 0.15, edge);

  // Inner / outer / both selection.
  int posMode = int(uPosition + 0.5);
  if (posMode == 0)      edge = edge * smoothstep(0.0, 0.1, outsideSum / 8.0); // outer
  else if (posMode == 1) edge = edge * smoothstep(0.0, 0.1, insideSum / 8.0);  // inner
  // posMode == 2 keeps both (no extra mask).

  // Animated crawling outline — modulate edge intensity with a moving
  // sine pattern along the gradient direction (marching ants style).
  if (uCrawl > 0.001) {
    float crawl = sin((vUv.x + vUv.y) * 80.0 - uTime * uCrawl * 6.0);
    edge *= 0.5 + 0.5 * crawl;
  }

  // Glow with adjustable falloff power.
  if (uGlow > 0.0) {
    float glowEdge = 0.0;
    for (int i = -2; i <= 2; i++) {
      for (int j = -2; j <= 2; j++) {
        vec2 off = vec2(float(i), float(j)) * texel * 2.0;
        vec4 nb4 = IMG_NORM_PIXEL(inputImage, vUv + off);
        float nb = mix(outlineLum(nb4.rgb), nb4.a, uAlphaAware);
        glowEdge += abs(nb - centerVal);
      }
    }
    glowEdge = glowEdge / 24.0;
    glowEdge = pow(smoothstep(0.02, 0.1, glowEdge), uGlowFalloff);
    edge = max(edge, glowEdge * uGlow * 0.7);
  }

  vec3 outColor = uColor * edge;
  vec3 finalColor;
  if (uOnly > 0.5) finalColor = outColor;
  else             finalColor = texColor.rgb + outColor;

  gl_FragColor = vec4(finalColor, texColor.a);
}
