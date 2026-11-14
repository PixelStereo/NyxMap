/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Raymarched heightmap terrain from image luminance with fly-over camera. Now supports horizon-fade-to-transparent and source-blend modes. Adapted from Ghost Arcade's Terrain effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced 3D",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "amount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Detail",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount3",
            "LABEL": "Amount3",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "threshold",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "horizonFade",
            "LABEL": "Horizon Fade",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.4,
            "DEFAULT": 0.7
        },
        {
            "NAME": "sourceMix",
            "LABEL": "Source Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (terrain3DShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(modeValue + 0.5))
#define uAmount (amount)
#define uAmount2 (amount2)
#define uAmount3 (amount3)
#define uThreshold (threshold)
#define uAngle (angle)
#define uCenter vec2(0.5, 0.5)
#define uColor vec3(0.05, 0.07, 0.12)
#define uHorizonFade (horizonFade)
#define uSourceMix (sourceMix)

          // 0=opaque sky, 1=fade horizon to transparent, 2=blend with source
        // height scale
       // camera height
       // speed
     // fog density
         // yaw
         // pitch/roll
          // fog colour
   // 0-1 fade terrain alpha at horizon
     // 0-1 blend with original source layer

#define PI 3.14159265359
#define TAU 6.28318530718

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float heightScale = uAmount * 0.8;
  float camHeight = 0.08 + uAmount2 * 0.35;
  float speed = uAmount3 * 0.5;
  float fogDensity = uThreshold * 3.0;
  float camYaw = uAngle;
  float camPitch = (uCenter.x - 0.5) * PI * 0.5;
  float camRoll = uCenter.y * TAU;
  vec3 fogColor = uColor;

  vec2 forward = vec2(cos(camYaw), sin(camYaw));
  vec2 camPos2D = forward * uTime * speed;

  vec3 color = vec3(0.0);
  float maxDist = 8.0;

  vec2 screenPos = uv * 2.0 - 1.0;
  screenPos.x *= uResolution.x / uResolution.y;

  vec3 ro = vec3(camPos2D.x, camHeight, camPos2D.y);
  vec3 rd = normalize(vec3(screenPos.x, screenPos.y * 0.5 - 0.3, 1.5));

  float crl = cos(camRoll), srl = sin(camRoll);
  rd = vec3(crl * rd.x - srl * rd.y, srl * rd.x + crl * rd.y, rd.z);
  float cp = cos(camPitch), sp = sin(camPitch);
  rd = vec3(rd.x, cp * rd.y - sp * rd.z, sp * rd.y + cp * rd.z);
  rd.xz = mat2(cos(camYaw), -sin(camYaw), sin(camYaw), cos(camYaw)) * rd.xz;

  float t = 0.01;
  bool hit = false;
  vec3 hitPos;
  for (int i = 0; i < 80; i++) {
    hitPos = ro + rd * t;
    vec2 sampleUV = fract(hitPos.xz * 0.3);
    float h = luma(IMG_NORM_PIXEL(inputImage, sampleUV).rgb) * heightScale;
    if (hitPos.y < h) { hit = true; break; }
    t += max(0.005, (hitPos.y - h) * 0.4);
    if (t > maxDist) break;
  }

  // Horizon fade: alpha drops as ray points up (toward sky) or distance grows
  float horizonW = smoothstep(0.0, 0.4, rd.y); // 0 below horizon, 1 well above
  float distFade = clamp(t / maxDist, 0.0, 1.0);
  float terrainAlpha = 1.0;
  int mode = int(uMode + 0.5);

  if (hit) {
    vec2 sUV = fract(hitPos.xz * 0.3);
    vec3 texColor = IMG_NORM_PIXEL(inputImage, sUV).rgb;
    float eps = 0.005;
    float hL = luma(IMG_NORM_PIXEL(inputImage, fract((hitPos.xz + vec2(-eps,0.0))*0.3)).rgb) * heightScale;
    float hR = luma(IMG_NORM_PIXEL(inputImage, fract((hitPos.xz + vec2(eps,0.0))*0.3)).rgb) * heightScale;
    float hD = luma(IMG_NORM_PIXEL(inputImage, fract((hitPos.xz + vec2(0.0,-eps))*0.3)).rgb) * heightScale;
    float hU = luma(IMG_NORM_PIXEL(inputImage, fract((hitPos.xz + vec2(0.0,eps))*0.3)).rgb) * heightScale;
    vec3 normal = normalize(vec3(hL - hR, 2.0 * eps, hD - hU));
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.3));
    float diff = max(dot(normal, lightDir), 0.0) * 0.8 + 0.2;
    color = texColor * diff;
    float fogFactor = 1.0 - exp(-t * fogDensity * 0.5);
    color = mix(color, fogColor, fogFactor);
    // Distance-based alpha fade
    terrainAlpha = mix(1.0, 1.0 - distFade, uHorizonFade * 0.85);
  } else {
    // Sky / miss
    float skyGrad = max(rd.y, 0.0);
    color = mix(fogColor, fogColor * 1.3, skyGrad);
    // Mode 0: opaque sky. Mode 1+: alpha falls off above horizon for transparent reveal.
    if (mode != 0) terrainAlpha = mix(1.0, 1.0 - horizonW, uHorizonFade);
  }

  // Mode 2: also blend final result with the underlying source colour
  vec4 srcLayer = src;
  if (mode == 2 && uSourceMix > 0.001) {
    color = mix(color, srcLayer.rgb, uSourceMix * (mode == 2 ? 1.0 : 0.0));
  }

  gl_FragColor = vec4(clamp(color, 0.0, 1.0), terrainAlpha * src.a);
}
