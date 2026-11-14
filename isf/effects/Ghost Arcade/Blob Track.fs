/*{
    "ISFVSN": "2",
    "DESCRIPTION": "GPU blob detection with shape markers, bounding boxes, and coordinate readouts — works on any media including point clouds. Adapted from Ghost Arcade's Blob Track effect.",
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
            "DEFAULT": 0.3
        },
        {
            "NAME": "blobShape",
            "LABEL": "Shape",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 4.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "blobColor",
            "LABEL": "Color",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 7.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "blobThickness",
            "LABEL": "Thickness",
            "TYPE": "float",
            "MIN": 0.5,
            "MAX": 5.0,
            "DEFAULT": 2.0
        },
        {
            "NAME": "blobMinSize",
            "LABEL": "Min Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 0.5,
            "DEFAULT": 0.02
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
            "LABEL": "Coords",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "blobShowBBox",
            "LABEL": "Brackets",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "blobShowCenter",
            "LABEL": "Centers",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "blobTrailLength",
            "LABEL": "Connectors",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "blobGridSize",
            "LABEL": "Grid",
            "TYPE": "float",
            "MIN": 4.0,
            "MAX": 48.0,
            "DEFAULT": 16.0
        },
        {
            "NAME": "blobMix",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.8
        },
        {
            "NAME": "blobColorMode",
            "LABEL": "Color Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "markerSize",
            "LABEL": "Marker Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "blendMode",
            "LABEL": "Blend Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (blobTrackShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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
#define uShowBBox (blobShowBBox)
#define uShowCenter (blobShowCenter)
#define uTrailLength (blobTrailLength)
#define uGridSize (blobGridSize)
#define uMix (blobMix)
#define uColorMode (blobColorMode)
#define uFixedColor vec3(0.0, 1.0, 0.4)
#define uMarkerSize (markerSize)
#define uBlendMode float(floor(blendMode + 0.5))

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
  float aspect = uResolution.x / uResolution.y;
  float gridRes = max(4.0, uGridSize);
  int shapeIdx = int(uShape);
  int colorIdx = int(uColor);
  float pixW = uThickness / uResolution.x;

  // Aggressive threshold curve for point-cloud compatibility.  Point clouds
  // render as sparse bright particles on a black background, so even tiny
  // luminance values should count.  We use a cube-root mapping scaled to
  float effThreshold = uThreshold;

  // Cell size in UV space — blob radius scales with grid cell size so that
  // changing gridSize smoothly rescales the detection from coarse to fine.
  float cellSize = 1.0 / gridRes;

  vec3 overlay = vec3(0.0);

  // Only check nearby cells (7x7 neighborhood around current pixel)
  vec2 myCell = floor(vUv * gridRes);

  for (int dy = -3; dy <= 3; dy++) {
    for (int dx = -3; dx <= 3; dx++) {
      vec2 cIdx = myCell + vec2(float(dx), float(dy));
      if (cIdx.x < 0.0 || cIdx.y < 0.0 || cIdx.x >= gridRes || cIdx.y >= gridRes) continue;

      float brightness = cellBright(cIdx, gridRes, inputImage);
      if (brightness < effThreshold) continue;
      if (!isPeak(cIdx, gridRes, effThreshold, inputImage)) continue;

      vec2 center = (cIdx + 0.5) / gridRes;

      // Blob radius scales with cell size so the grid parameter works
      // smoothly: coarse grid = big markers, fine grid = small markers.
      // Brightness still modulates the size within each cell.
      float blobR = cellSize * mix(0.25, 0.7, brightness) * (0.6 + uThickness * 0.2) * uMarkerSize;
      if (blobR < uMinSize * cellSize) continue;

      // Pulse
      float pulse = 1.0 + 0.1 * sin(uTime * 3.0 + cIdx.x * 3.7 + cIdx.y * 5.3);
      float r = blobR * pulse;

      // Color mode: 0=auto (getTrackColor), 1=fixed (user color), 2=spectrum (rainbow)
      int cm = int(uColorMode);
      vec3 tColor;
      if (cm == 1) {
        tColor = uFixedColor;
      } else if (cm == 2) {
        // Spectrum: map brightness to full rainbow hue
        tColor = hsl2rgb(brightness, 1.0, 0.5);
      } else {
        tColor = getTrackColor(colorIdx, IMG_NORM_PIXEL(inputImage, center).rgb);
      }
      // Boost dim blobs aggressively — point clouds often have very low
      // overall brightness, so we use an inverse-brightness boost: the
      // dimmer the source pixel, the brighter the marker overlay becomes.
      float boostFactor = mix(1.8, 0.9, brightness); // dim→1.8x, bright→0.9x
      tColor *= boostFactor;

      // Aspect-corrected distance
      vec2 diff = vUv - center;
      diff.x *= aspect;
      float dist = length(diff);

      // ── Shape marker (outline) ──
      float markerAlpha = 0.0;

      if (shapeIdx == 0) {
        // Circle outline
        markerAlpha = smoothstep(pixW, 0.0, abs(dist - r));
      }
      else if (shapeIdx == 1) {
        // Square outline
        vec2 ad = abs(diff);
        float boxDist = max(ad.x - r, ad.y - r);
        markerAlpha = smoothstep(pixW, 0.0, abs(boxDist));
      }
      else if (shapeIdx == 2) {
        // Triangle outline (equilateral, pointing up)
        vec2 p = diff;
        float k = 1.732; // sqrt(3)
        float e1 = p.y + r * 0.5;
        float e2 = -0.5 * p.y + k * 0.5 * p.x - r * 0.5;
        float e3 = -0.5 * p.y - k * 0.5 * p.x - r * 0.5;
        float d1 = abs(e1) / 1.0;
        float d2 = abs(e2) / 1.0;
        float d3 = abs(e3) / 1.0;
        float minE = min(min(d1, d2), d3);
        float triOutline = smoothstep(pixW * 1.5, 0.0, minE) * step(dist, r * 2.0);
        markerAlpha = triOutline;
      }
      else if (shapeIdx == 3) {
        // Diamond outline
        float diamondDist = abs(diff.x) + abs(diff.y) - r;
        markerAlpha = smoothstep(pixW, 0.0, abs(diamondDist));
      }
      else {
        // Crosshair
        float armH = smoothstep(pixW * 1.2, 0.0, abs(diff.y)) * step(dist, r * 1.3);
        float armV = smoothstep(pixW * 1.2, 0.0, abs(diff.x)) * step(dist, r * 1.3);
        float ring = smoothstep(pixW, 0.0, abs(dist - r * 0.7));
        markerAlpha = max(max(armH, armV), ring);
      }

      overlay += tColor * markerAlpha;

      // ── Center dot ──
      if (uShowCenter > 0.5) {
        float cd = smoothstep(pixW * 3.0, 0.0, dist);
        overlay += tColor * cd * 0.9;
      }

      // ── Bounding box ──
      if (uShowBBox > 0.5) {
        float bboxR = r * 1.6;
        vec2 bMin = center - vec2(bboxR / aspect, bboxR);
        vec2 bMax = center + vec2(bboxR / aspect, bboxR);
        float inX = step(bMin.x, vUv.x) * step(vUv.x, bMax.x);
        float inY = step(bMin.y, vUv.y) * step(vUv.y, bMax.y);
        float bL = smoothstep(pixW * 0.6, 0.0, abs(vUv.x - bMin.x)) * inY;
        float bR = smoothstep(pixW * 0.6, 0.0, abs(vUv.x - bMax.x)) * inY;
        float bT = smoothstep(pixW * 0.6, 0.0, abs(vUv.y - bMax.y)) * inX;
        float bB = smoothstep(pixW * 0.6, 0.0, abs(vUv.y - bMin.y)) * inX;
        float box = min(max(max(bL, bR), max(bT, bB)), 1.0);
        overlay += tColor * box * 0.5;
      }

      // ── Coordinate numbers ──
      if (uShowCoords > 0.5) {
        float fs = 0.006;
        vec2 textPos = center + vec2(r * 2.0 / aspect, r * 0.5);
        vec2 tp = (vUv - textPos);
        tp.x *= aspect;
        int fakeX = int(mod(cIdx.x * 47.0 + uTime * 12.0, 999.0));
        int fakeY = int(mod(cIdx.y * 31.0 + uTime * 8.0, 999.0));
        float xn = drawNum3(tp, fakeX, fs);
        float yn = drawNum3(tp - vec2(0.0, -fs * 1.8), fakeY, fs);
        overlay += tColor * max(xn, yn) * 0.85;
      }

      // ── Connector lines to neighboring blobs ──
      if (uTrailLength > 0.01) {
        // Line thickness scales with uThickness param — 2x to 8x a single pixel
        float lineW = pixW * mix(2.0, 8.0, (uThickness - 0.5) / 4.5);
        float maxDist = uTrailLength * 0.5;
        for (int cy = -2; cy <= 2; cy++) {
          for (int cx = -2; cx <= 2; cx++) {
            if (cx == 0 && cy == 0) continue;
            // Only check one direction to avoid double-drawing
            if (cy < 0 || (cy == 0 && cx < 0)) continue;
            vec2 oCell = cIdx + vec2(float(cx), float(cy));
            if (oCell.x < 0.0 || oCell.y < 0.0 || oCell.x >= gridRes || oCell.y >= gridRes) continue;
            float oB = cellBright(oCell, gridRes, inputImage);
            if (oB < effThreshold) continue;
            if (!isPeak(oCell, gridRes, effThreshold, inputImage)) continue;

            vec2 oCenter = (oCell + 0.5) / gridRes;
            vec2 ab = oCenter - center;
            float abLen = length(ab * vec2(aspect, 1.0));
            if (abLen > maxDist || abLen < 0.001) continue;

            // Line segment distance
            vec2 pa = vUv - center;
            float t = clamp(dot(pa, ab) / dot(ab, ab), 0.0, 1.0);
            vec2 closest = center + ab * t;
            float ld = length((vUv - closest) * vec2(aspect, 1.0));
            float fade = 1.0 - abLen / maxDist;
            // Dashed
            float dash = step(0.4, fract(t * 8.0 + uTime * 2.0));
            float line = smoothstep(lineW, 0.0, ld) * fade * dash * 0.7;
            overlay += tColor * line;
          }
        }
      }
    }
  }

  // Blend modes: 0=Add, 1=Screen, 2=Multiply, 3=Overlay, 4=Replace
  vec3 blended;
  if (uBlendMode < 0.5) {
    blended = original + overlay; // Add
  } else if (uBlendMode < 1.5) {
    blended = 1.0 - (1.0 - original) * (1.0 - overlay); // Screen
  } else if (uBlendMode < 2.5) {
    blended = original * (1.0 + overlay * 2.0); // Multiply (boosted)
  } else if (uBlendMode < 3.5) {
    // Overlay: dark areas multiply, bright areas screen
    vec3 lo = 2.0 * original * overlay;
    vec3 hi = 1.0 - 2.0 * (1.0 - original) * (1.0 - overlay);
    blended = mix(lo, hi, step(0.5, original));
  } else {
    blended = overlay; // Replace
  }
  vec3 finalColor = mix(original, blended, uMix);
  // Force alpha=1 when overlay is present so overlays show outside transparent areas (3D models, point clouds)
  float overlayPresence = step(0.001, length(overlay));
  float finalAlpha = max(src.a, overlayPresence * uMix);
  gl_FragColor = vec4(finalColor, finalAlpha);
}
