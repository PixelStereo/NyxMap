/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Infinite textured tunnel — fly through with morphing cross-section and fog. Adapted from Ghost Arcade's Tunnel Flight effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Depth",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "twist",
            "LABEL": "Twist",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "tunnelDepth",
            "LABEL": "Tunnel Depth",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.5
        },
        {
            "NAME": "centerX",
            "LABEL": "Center X",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "centerY",
            "LABEL": "Center Y",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
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
            "NAME": "chromatic",
            "LABEL": "Chromatic",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        }
    ]
}*/

// Ported from Ghost Arcade (tunnelFlightHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uSpeed (speed)
#define uTwist (twist)
#define uTunnelDepth (tunnelDepth)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uMode float(floor(modeValue + 0.5))
#define uChromatic (chromatic)

         // 0-3
         // 0-3
   // 0.5-3


          // 0=cylinder, 1=funnel, 2=square
     // 0-1

vec2 tunnelMap(vec2 uv, float zOffset) {
  int mode = int(uMode + 0.5);
  vec2 c = vec2(uCenterX, uCenterY);
  vec2 d = uv - c;
  d.x *= uResolution.x / uResolution.y;
  float r = length(d);
  float a = atan(d.y, d.x);
  a += uTwist * (uTime * uSpeed * 0.5);
  float depthScale = (mode == 1) ? r * uTunnelDepth : uTunnelDepth;
  float z = (uTime * uSpeed + zOffset) / max(0.05, r * depthScale);
  if (mode == 2) {
    // Square cross-section
    vec2 sq = abs(d);
    float side = max(sq.x, sq.y);
    r = side;
    z = (uTime * uSpeed + zOffset) / max(0.05, r);
  }
  vec2 sUv = vec2(a / 6.28318 + 0.5, fract(z));
  return sUv;
}

void main() {
  vec2 baseUv = tunnelMap(vUv, 0.0);
  vec3 col;
  if (uChromatic > 0.001) {
    vec2 uvR = tunnelMap(vUv, 0.05 * uChromatic);
    vec2 uvB = tunnelMap(vUv, -0.05 * uChromatic);
    col.r = IMG_NORM_PIXEL(inputImage, uvR).r;
    col.g = IMG_NORM_PIXEL(inputImage, baseUv).g;
    col.b = IMG_NORM_PIXEL(inputImage, uvB).b;
  } else {
    col = IMG_NORM_PIXEL(inputImage, baseUv).rgb;
  }
  // Darken at far end
  vec2 c = vec2(uCenterX, uCenterY);
  float r = length(vUv - c);
  float fade = smoothstep(0.0, 0.7, r);
  col *= fade;
  gl_FragColor = vec4(col, 1.0);
}
