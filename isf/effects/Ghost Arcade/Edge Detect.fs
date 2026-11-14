/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Sobel/Laplacian edge detection. Adapted from Ghost Arcade's Edge Detect effect.",
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
            "NAME": "edgeThreshold",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
        },
        {
            "NAME": "thicknessValue",
            "LABEL": "Thickness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
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
            "NAME": "invertValue",
            "LABEL": "Invert",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "edgeR",
            "LABEL": "Edge R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "edgeG",
            "LABEL": "Edge G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "edgeB",
            "LABEL": "Edge B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintEdges",
            "LABEL": "Tint Edges",
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
            "NAME": "edgeOnly",
            "LABEL": "Edge Only",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (edgeDetectShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uThreshold (edgeThreshold)
#define uThickness (thicknessValue)
#define uMode float(floor(modeValue + 0.5))
#define uInvert (invertValue)
#define uEdgeR (edgeR)
#define uEdgeG (edgeG)
#define uEdgeB (edgeB)
#define uTintEdges (tintEdges)
#define uGlow (glowValue)
#define uEdgeOnly (edgeOnly)

   // 0-1 edge threshold
   // 0.5-3 line thickness
        // 0=sobel, 1=laplacian, 2=prewitt, 3=frei-chen, 4=color-edge
      // 0=normal, 1=inverted
       // 0-1 edge tint R (when uTintEdges > 0)


   // 0-1 — 0=white edges (legacy), 1=full tint color
        // 0-1 — bloom-style glow around edges
    // 0=show image+edges, 1=transparent fill (edges-only alpha)

float edgeLum(vec3 c) {
  return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
  vec2 texel = uThickness / uResolution;

  // Sample 3x3 neighborhood
  float tl = edgeLum(IMG_NORM_PIXEL(inputImage, vUv + vec2(-texel.x, texel.y)).rgb);
  float tc = edgeLum(IMG_NORM_PIXEL(inputImage, vUv + vec2(0.0, texel.y)).rgb);
  float tr = edgeLum(IMG_NORM_PIXEL(inputImage, vUv + vec2(texel.x, texel.y)).rgb);
  float ml = edgeLum(IMG_NORM_PIXEL(inputImage, vUv + vec2(-texel.x, 0.0)).rgb);
  float mc = edgeLum(IMG_NORM_PIXEL(inputImage, vUv).rgb);
  float mr = edgeLum(IMG_NORM_PIXEL(inputImage, vUv + vec2(texel.x, 0.0)).rgb);
  float bl = edgeLum(IMG_NORM_PIXEL(inputImage, vUv + vec2(-texel.x, -texel.y)).rgb);
  float bc = edgeLum(IMG_NORM_PIXEL(inputImage, vUv + vec2(0.0, -texel.y)).rgb);
  float br = edgeLum(IMG_NORM_PIXEL(inputImage, vUv + vec2(texel.x, -texel.y)).rgb);

  float edge = 0.0;

  // Use float comparisons for WebGL compatibility
  if (uMode < 0.5) {
    // Sobel operator
    float gx = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
    float gy = -tl - 2.0*tc - tr + bl + 2.0*bc + br;
    edge = sqrt(gx*gx + gy*gy);
  } else if (uMode < 1.5) {
    // Laplacian operator
    edge = abs(-4.0*mc + tc + ml + mr + bc);
  } else if (uMode < 2.5) {
    // Prewitt operator
    float gx = -tl - ml - bl + tr + mr + br;
    float gy = -tl - tc - tr + bl + bc + br;
    edge = sqrt(gx*gx + gy*gy);
  } else {
    // Frei-Chen operator (more isotropic)
    float sq2 = 1.41421;
    float gx = -tl - sq2*ml - bl + tr + sq2*mr + br;
    float gy = -tl - sq2*tc - tr + bl + sq2*bc + br;
    edge = sqrt(gx*gx + gy*gy) / (2.0 + sq2);
  }

  // Color-edge mode (uMode=4) — compute Sobel per channel and combine.
  if (uMode > 3.5) {
    vec3 cTL = IMG_NORM_PIXEL(inputImage, vUv + vec2(-texel.x, texel.y)).rgb;
    vec3 cTR = IMG_NORM_PIXEL(inputImage, vUv + vec2(texel.x, texel.y)).rgb;
    vec3 cBL = IMG_NORM_PIXEL(inputImage, vUv + vec2(-texel.x, -texel.y)).rgb;
    vec3 cBR = IMG_NORM_PIXEL(inputImage, vUv + vec2(texel.x, -texel.y)).rgb;
    vec3 cML = IMG_NORM_PIXEL(inputImage, vUv + vec2(-texel.x, 0.0)).rgb;
    vec3 cMR = IMG_NORM_PIXEL(inputImage, vUv + vec2(texel.x, 0.0)).rgb;
    vec3 cTC = IMG_NORM_PIXEL(inputImage, vUv + vec2(0.0, texel.y)).rgb;
    vec3 cBC = IMG_NORM_PIXEL(inputImage, vUv + vec2(0.0, -texel.y)).rgb;
    vec3 gxV = -cTL - 2.0*cML - cBL + cTR + 2.0*cMR + cBR;
    vec3 gyV = -cTL - 2.0*cTC - cTR + cBL + 2.0*cBC + cBR;
    vec3 edgeRGB = sqrt(gxV*gxV + gyV*gyV);
    // Per-channel threshold + invert.
    edgeRGB = smoothstep(vec3(uThreshold * 0.3), vec3(uThreshold * 0.8 + 0.02), edgeRGB);
    if (uInvert > 0.5) edgeRGB = 1.0 - edgeRGB;
    vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
    vec3 finalCol = edgeRGB;
    float a = uEdgeOnly > 0.5 ? max(max(edgeRGB.r, edgeRGB.g), edgeRGB.b) : texColor.a;
    gl_FragColor = vec4(finalCol, a);
    return;
  }

  edge = smoothstep(uThreshold * 0.3, uThreshold * 0.8 + 0.02, edge);
  if (uInvert > 0.5) edge = 1.0 - edge;

  // Tint edges from white to user color. uTintEdges=0 keeps legacy
  // monochrome white edges; =1 fully replaces with the tint color.
  vec3 tint = mix(vec3(1.0), vec3(uEdgeR, uEdgeG, uEdgeB), uTintEdges);
  vec3 edgeColor = tint * edge;

  // Glow — sample_ a wider neighborhood and add a bloom around edges.
  if (uGlow > 0.001) {
    float glowSum = 0.0;
    for (int gi = -2; gi <= 2; gi++) {
      for (int gj = -2; gj <= 2; gj++) {
        vec2 off = vec2(float(gi), float(gj)) / uResolution * (2.0 + uGlow * 4.0);
        float l = edgeLum(IMG_NORM_PIXEL(inputImage, vUv + off).rgb);
        glowSum += l;
      }
    }
    glowSum = (glowSum / 25.0) * uGlow * 0.6;
    edgeColor += tint * glowSum;
  }

  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  if (uEdgeOnly > 0.5) {
    // Transparent body, edges only — alpha proportional to edge strength.
    gl_FragColor = vec4(edgeColor, edge);
  } else {
    gl_FragColor = vec4(edgeColor, texColor.a);
  }
}
