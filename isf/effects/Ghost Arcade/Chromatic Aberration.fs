/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Lens color fringing from center. Adapted from Ghost Arcade's Chromatic Aberration effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Light & Glow",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "amount",
            "LABEL": "Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
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
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
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
            "NAME": "edgeFalloff",
            "LABEL": "Edge Falloff",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "mixValue",
            "LABEL": "Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (chromaticAberrationHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uMode float(floor(modeValue + 0.5))
#define uAngle (angle)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uEdgeFalloff (edgeFalloff)
#define uMix (mixValue)

        // 0-1 strength
          // 0=linear, 1=radial, 2=lens (cubic), 3=prism
         // 0-360 (linear)
       // 0-1
       // 0-1
   // 0-1 weight by distance from center
           // 0-1

void main() {
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  if (uAmount < 0.001) { gl_FragColor = src; return; }

  int mode = int(uMode + 0.5);
  vec2 center = vec2(uCenterX, uCenterY);
  vec2 d = vUv - center;
  float dist = length(d);

  vec2 dirR, dirB;
  float strength = uAmount * 0.05;

  if (mode == 0) {
    // Linear (directional)
    float ang = radians(uAngle);
    vec2 dir = vec2(cos(ang), sin(ang));
    dirR =  dir * strength;
    dirB = -dir * strength;
  } else if (mode == 1) {
    // Radial outward
    vec2 nd = (dist > 0.001) ? d / dist : vec2(1.0, 0.0);
    dirR =  nd * strength;
    dirB = -nd * strength;
  } else if (mode == 2) {
    // Lens (cubic falloff — like real glass)
    float k = strength * (dist * dist * 4.0);
    vec2 nd = (dist > 0.001) ? d / dist : vec2(1.0, 0.0);
    dirR =  nd * k;
    dirB = -nd * k;
  } else {
    // Prism rainbow spread
    vec2 nd = (dist > 0.001) ? d / dist : vec2(1.0, 0.0);
    dirR =  nd * strength * 1.5;
    dirB = -nd * strength * 1.5;
  }

  // Edge falloff weight (only push at the edges)
  float weight = mix(1.0, dist * 2.0, uEdgeFalloff);

  vec2 offR = dirR * weight;
  vec2 offB = dirB * weight;
  float r = IMG_NORM_PIXEL(inputImage, vUv + offR).r;
  float g = IMG_NORM_PIXEL(inputImage, vUv).g;
  float b = IMG_NORM_PIXEL(inputImage, vUv + offB).b;
  vec3 result = vec3(r, g, b);

  // Prism mode adds extra mid-spectrum tints
  if (mode == 3) {
    vec2 nd = (dist > 0.001) ? d / dist : vec2(1.0, 0.0);
    float yE = IMG_NORM_PIXEL(inputImage, vUv + nd * strength * 0.7 * weight).r * 0.5
             + IMG_NORM_PIXEL(inputImage, vUv + nd * strength * 0.7 * weight).g * 0.5;
    result.r = mix(result.r, max(result.r, yE), 0.3);
    result.g = mix(result.g, yE, 0.2);
  }

  gl_FragColor = vec4(mix(src.rgb, result, uMix), src.a);
}
