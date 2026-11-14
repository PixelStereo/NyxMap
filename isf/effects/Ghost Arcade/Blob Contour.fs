/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Edge-detection contour lines with topographic multi-level luminance visualization and optional coordinate overlay. Adapted from Ghost Arcade's Blob Contour effect.",
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
            "NAME": "blobThreshold",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "blobShape",
            "LABEL": "Style",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "blobColor",
            "LABEL": "Color",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 7.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "blobThickness",
            "LABEL": "Thickness",
            "TYPE": "float",
            "MIN": 0.5,
            "MAX": 5.0,
            "DEFAULT": 1.5
        },
        {
            "NAME": "blobMinSize",
            "LABEL": "Levels",
            "TYPE": "float",
            "MIN": 0.05,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "maxBlobs",
            "LABEL": "Max Blobs",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 64.0,
            "DEFAULT": 32.0
        },
        {
            "NAME": "blobShowCoords",
            "LABEL": "Values",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "showBBox",
            "LABEL": "Show BBox",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "showCenter",
            "LABEL": "Show Center",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "blobTrailLength",
            "LABEL": "Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "gridSize",
            "LABEL": "Grid Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 32.0,
            "DEFAULT": 16.0
        },
        {
            "NAME": "blobMix",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.7
        }
    ]
}*/

// Ported from Ghost Arcade (blobContourShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uThreshold (blobThreshold)
#define uShape (blobShape)
#define uColor (blobColor)
#define uThickness (blobThickness)
#define uMinSize (blobMinSize)
#define uMaxBlobs (maxBlobs)
#define uShowCoords (blobShowCoords)
#define uShowBBox (showBBox)
#define uShowCenter (showCenter)
#define uTrailLength (blobTrailLength)
#define uGridSize (gridSize)
#define uMix (blobMix)

#define PI 3.14159265359

float hash21(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float luma(vec3 c) {
  return dot(c, vec3(0.299, 0.587, 0.114));
}

vec3 getTrackColor(int idx, vec3 srcColor) {
  if (idx == 0) return vec3(0.0, 1.0, 0.4);
  if (idx == 1) return vec3(0.0, 0.9, 1.0);
  if (idx == 2) return vec3(1.0, 0.0, 0.8);
  if (idx == 3) return vec3(1.0, 0.75, 0.0);
  if (idx == 4) return vec3(1.0, 0.15, 0.15);
  if (idx == 5) return vec3(0.2, 0.4, 1.0);
  if (idx == 6) return vec3(1.0, 1.0, 1.0);
  return srcColor;
}

// Simple 7-segment digit
float seg(vec2 p, vec2 a, vec2 b, float w) {
  vec2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return smoothstep(w, w * 0.3, length(pa - ba * h));
}

float digit(vec2 p, int d, float s) {
  float w = s * 0.1;
  float h = s * 0.5;
  float hw = s * 0.22;
  float top = seg(p, vec2(-hw, h), vec2(hw, h), w);
  float tr  = seg(p, vec2(hw, h), vec2(hw, 0.0), w);
  float br  = seg(p, vec2(hw, 0.0), vec2(hw, -h), w);
  float bot = seg(p, vec2(-hw, -h), vec2(hw, -h), w);
  float bl  = seg(p, vec2(-hw, -h), vec2(-hw, 0.0), w);
  float tl  = seg(p, vec2(-hw, 0.0), vec2(-hw, h), w);
  float mid = seg(p, vec2(-hw, 0.0), vec2(hw, 0.0), w);
  if (d==0) return max(max(max(top,tr),max(br,bot)),max(bl,tl));
  if (d==1) return max(tr,br);
  if (d==2) return max(max(max(top,tr),mid),max(bot,bl));
  if (d==3) return max(max(max(top,tr),max(br,bot)),mid);
  if (d==4) return max(max(tl,mid),max(tr,br));
  if (d==5) return max(max(max(top,tl),mid),max(br,bot));
  if (d==6) return max(max(max(top,tl),max(mid,bl)),max(br,bot));
  if (d==7) return max(top,max(tr,br));
  if (d==8) return max(max(max(top,tr),max(br,bot)),max(max(bl,tl),mid));
  if (d==9) return max(max(max(top,tl),max(tr,mid)),max(br,bot));
  return 0.0;
}

float drawNum3(vec2 p, int num, float s) {
  int d2 = num / 100;
  int d1 = (num - d2 * 100) / 10;
  int d0 = num - d2 * 100 - d1 * 10;
  float sp = s * 0.6;
  float v = 0.0;
  if (d2 > 0) v = max(v, digit(p + vec2(sp, 0.0), d2, s));
  v = max(v, digit(p, d1, s));
  v = max(v, digit(p - vec2(sp, 0.0), d0, s));
  return v;
}

// Check brightness at a grid cell center — 5-tap cross pattern
float cellBright(vec2 cellIdx, float gridRes, sampler2D tex) {
  vec2 uv = (cellIdx + 0.5) / gridRes;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 0.0;
  float halfCell = 0.5 / gridRes;
  float peak = luma(texture2D(tex, uv).rgb);
  peak = max(peak, luma(texture2D(tex, uv + vec2( halfCell,  0.0)).rgb));
  peak = max(peak, luma(texture2D(tex, uv + vec2(-halfCell,  0.0)).rgb));
  peak = max(peak, luma(texture2D(tex, uv + vec2( 0.0,  halfCell)).rgb));
  peak = max(peak, luma(texture2D(tex, uv + vec2( 0.0, -halfCell)).rgb));
  return peak;
}

// HSL to RGB conversion for spectrum color mode
vec3 hsl2rgb(float h, float s, float l) {
  float c = (1.0 - abs(2.0 * l - 1.0)) * s;
  float x = c * (1.0 - abs(mod(h * 6.0, 2.0) - 1.0));
  float m = l - c * 0.5;
  vec3 rgb;
  if (h < 1.0/6.0)      rgb = vec3(c, x, 0.0);
  else if (h < 2.0/6.0) rgb = vec3(x, c, 0.0);
  else if (h < 3.0/6.0) rgb = vec3(0.0, c, x);
  else if (h < 4.0/6.0) rgb = vec3(0.0, x, c);
  else if (h < 5.0/6.0) rgb = vec3(x, 0.0, c);
  else                   rgb = vec3(c, 0.0, x);
  return rgb + m;
}

// Is this cell brighter than all 4 cardinal neighbors? (local peak)
bool isPeak(vec2 cellIdx, float gridRes, float threshold, sampler2D tex) {
  float b = cellBright(cellIdx, gridRes, tex);
  if (b < threshold) return false;
  float n1 = cellBright(cellIdx + vec2(1.0, 0.0), gridRes, tex);
  float n2 = cellBright(cellIdx + vec2(-1.0, 0.0), gridRes, tex);
  float n3 = cellBright(cellIdx + vec2(0.0, 1.0), gridRes, tex);
  float n4 = cellBright(cellIdx + vec2(0.0, -1.0), gridRes, tex);
  return (b >= n1 && b >= n2 && b >= n3 && b >= n4);
}


void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 original = src.rgb;
  vec2 texel = 1.0 / uResolution;

  // Sobel edge detection
  float tl = luma(IMG_NORM_PIXEL(inputImage, vUv + vec2(-texel.x, texel.y)).rgb);
  float t  = luma(IMG_NORM_PIXEL(inputImage, vUv + vec2(0.0, texel.y)).rgb);
  float tr = luma(IMG_NORM_PIXEL(inputImage, vUv + vec2(texel.x, texel.y)).rgb);
  float l  = luma(IMG_NORM_PIXEL(inputImage, vUv + vec2(-texel.x, 0.0)).rgb);
  float c  = luma(src.rgb);
  float r  = luma(IMG_NORM_PIXEL(inputImage, vUv + vec2(texel.x, 0.0)).rgb);
  float bl = luma(IMG_NORM_PIXEL(inputImage, vUv + vec2(-texel.x, -texel.y)).rgb);
  float b  = luma(IMG_NORM_PIXEL(inputImage, vUv + vec2(0.0, -texel.y)).rgb);
  float br = luma(IMG_NORM_PIXEL(inputImage, vUv + vec2(texel.x, -texel.y)).rgb);

  float gx = -tl - 2.0*l - bl + tr + 2.0*r + br;
  float gy = -tl - 2.0*t - tr + bl + 2.0*b + br;
  float edge = sqrt(gx*gx + gy*gy);

  float contourThick = uThickness * 0.3;
  int levels = int(max(1.0, uMinSize * 20.0));
  float contour = 0.0;
  int style = int(uShape);

  for (int i = 1; i <= 20; i++) {
    if (i > levels) break;
    float levelVal = float(i) / float(levels + 1);
    float dist = abs(c - levelVal);
    float lw = contourThick * texel.x * 0.5;
    if (style == 0) {
      contour = max(contour, smoothstep(lw, 0.0, dist));
    } else if (style == 1) {
      contour = max(contour, step(dist, lw));
    } else {
      float dashPhase = fract(vUv.x * uResolution.x * 0.05 + uTime * 2.0);
      contour = max(contour, smoothstep(lw, 0.0, dist) * step(0.4, dashPhase));
    }
  }

  float edgeLine = smoothstep(uThreshold * 0.5, uThreshold, edge);
  contour = max(contour * 0.8, edgeLine * 0.6);

  int colorIdx = int(uColor);
  vec3 tColor = getTrackColor(colorIdx, src.rgb);
  float glow = uTrailLength;
  vec3 contourColor = tColor * (1.0 + glow * 2.0 * contour);
  float scan = sin(vUv.y * uResolution.y * 0.5 + uTime * 5.0) * 0.5 + 0.5;
  contourColor *= 0.9 + 0.1 * scan;

  float coordOverlay = 0.0;
  if (uShowCoords > 0.5) {
    vec2 gridCell = floor(vUv * 20.0);
    vec2 cellUv = fract(vUv * 20.0);
    float cellLuma = luma(IMG_NORM_PIXEL(inputImage, (gridCell + 0.5) / 20.0).rgb);
    int lumaVal = int(cellLuma * 99.0);
    coordOverlay = drawNum3((cellUv - 0.5) * 0.5, lumaVal, 0.04) * 0.6;
  }

  vec3 ov = contourColor * contour + tColor * coordOverlay;
  vec3 finalColor = mix(original, original + ov, uMix);
  float overlayPresence = step(0.001, contour + coordOverlay);
  float finalAlpha = max(src.a, overlayPresence * uMix);
  gl_FragColor = vec4(finalColor, finalAlpha);
}
