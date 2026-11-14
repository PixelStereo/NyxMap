/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Density heat-map overlay with configurable palettes (inferno, viridis, plasma, magma), grid lines, and peak markers. Adapted from Ghost Arcade's Blob Heatmap effect.",
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
            "DEFAULT": 0.2
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
            "LABEL": "Palette",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "blobThickness",
            "LABEL": "Line Width",
            "TYPE": "float",
            "MIN": 0.5,
            "MAX": 5.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "minSize",
            "LABEL": "Min Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
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
            "LABEL": "Values",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "blobShowBBox",
            "LABEL": "Grid Lines",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "blobShowCenter",
            "LABEL": "Peaks",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "trailLength",
            "LABEL": "Trail Length",
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
            "DEFAULT": 0.85
        }
    ]
}*/

// Ported from Ghost Arcade (blobHeatmapShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uThreshold (blobThreshold)
#define uShape (blobShape)
#define uColor (blobColor)
#define uThickness (blobThickness)
#define uMinSize (minSize)
#define uMaxBlobs (maxBlobs)
#define uShowCoords (blobShowCoords)
#define uShowBBox (blobShowBBox)
#define uShowCenter (blobShowCenter)
#define uTrailLength (trailLength)
#define uGridSize (blobGridSize)
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


vec3 inferno(float t) {
  vec3 c0=vec3(0,0,.04), c1=vec3(.35,0,.5), c2=vec3(.85,.2,.15), c3=vec3(1,.85,.1), c4=vec3(1,1,.85);
  if(t<.25) return mix(c0,c1,t*4.);
  if(t<.5)  return mix(c1,c2,(t-.25)*4.);
  if(t<.75) return mix(c2,c3,(t-.5)*4.);
  return mix(c3,c4,(t-.75)*4.);
}
vec3 viridis(float t) {
  vec3 c0=vec3(.27,0,.33), c1=vec3(.28,.47,.64), c2=vec3(.13,.72,.55), c3=vec3(.99,.91,.14);
  if(t<.33) return mix(c0,c1,t*3.);
  if(t<.66) return mix(c1,c2,(t-.33)*3.);
  return mix(c2,c3,(t-.66)*3.);
}
vec3 heatColor(float t, int pal) {
  t = clamp(t,0.,1.);
  if(pal==0) return inferno(t);
  if(pal==1) return viridis(t);
  if(pal==2) return mix(mix(vec3(.05,0,.53),vec3(.8,.12,.56),t), mix(vec3(.8,.12,.56),vec3(.94,.98,.13),t), t);
  return mix(mix(vec3(0,0,.02),vec3(.7,.1,.45),t), mix(vec3(.7,.1,.45),vec3(1,1,.75),t), t);
}

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  vec3 original = src.rgb;
  float gridRes = max(4.0, uGridSize);
  int paletteIdx = int(uColor);

  vec2 cellIdx = floor(vUv * gridRes);
  vec2 cellCenter = (cellIdx + 0.5) / gridRes;
  float cellB = luma(IMG_NORM_PIXEL(inputImage, cellCenter).rgb);
  float intensity = smoothstep(uThreshold * 0.5, uThreshold + 0.3, cellB);

  int style = int(uShape);
  vec3 heat;
  if(style==0) heat = heatColor(intensity, paletteIdx);
  else if(style==1) heat = heatColor(floor(intensity*8.)/8., paletteIdx);
  else { heat = heatColor(intensity, paletteIdx); heat += hash21(vUv*uResolution+uTime)*0.05; }
  heat *= intensity;

  float gridOv = 0.0;
  if(uShowBBox > 0.5) {
    vec2 cu = fract(vUv * gridRes);
    float lw = uThickness * 0.002;
    gridOv = min(step(cu.x,lw)+step(1.-lw,cu.x)+step(cu.y,lw)+step(1.-lw,cu.y), 1.0) * 0.3;
  }

  float numOv = 0.0;
  if(uShowCoords > 0.5) {
    vec2 cu = fract(vUv * gridRes);
    numOv = drawNum3((cu-vec2(.5,.3))*2., int(cellB*99.), 0.06) * 0.7;
  }

  float peakOv = 0.0;
  if(uShowCenter > 0.5 && intensity > 0.8) {
    vec2 cu = fract(vUv * gridRes) - 0.5;
    peakOv = smoothstep(.003,0., abs(length(cu)-.15));
    peakOv += smoothstep(.003,0., abs(cu.x)) * step(abs(cu.y),.2);
    peakOv += smoothstep(.003,0., abs(cu.y)) * step(abs(cu.x),.2);
    peakOv *= 0.8;
  }

  vec3 tc = heatColor(1.0, paletteIdx);
  vec3 ov = heat + tc * (gridOv + numOv + peakOv);
  vec3 finalColor = mix(original, ov, uMix);
  float overlayPresence = step(0.001, length(ov));
  float finalAlpha = max(src.a, overlayPresence * uMix);
  gl_FragColor = vec4(finalColor, finalAlpha);
}
