/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Rotational twirl distortion from center. Adapted from Ghost Arcade's Twirl effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Distort",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.5
        },
        {
            "NAME": "radius",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
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
            "NAME": "falloffValue",
            "LABEL": "Falloff",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 3.0,
            "DEFAULT": 1.5
        },
        {
            "NAME": "animSpeed",
            "LABEL": "Anim Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
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

// Ported from Ghost Arcade (twirlHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAngle (angle)
#define uRadius (radius)
#define uCenterX (centerX)
#define uCenterY (centerY)
#define uFalloff (falloffValue)
#define uAnimSpeed (animSpeed)
#define uMix (mixValue)

         // radians of full twirl at center
        // 0.05-1 area of effect
       // 0-1
       // 0-1
       // 0.5-4 power curve
     // 0-2 auto-rotation
           // 0-1

void main() {
  vec2 center = vec2(uCenterX, uCenterY);
  vec2 d = vUv - center;
  d.x *= uResolution.x / uResolution.y;
  float r = length(d);
  float maxR = max(0.001, uRadius);
  float falloff = pow(clamp(1.0 - r / maxR, 0.0, 1.0), max(0.5, uFalloff));
  float a = uAngle * falloff + uTime * uAnimSpeed;
  float ca = cos(a), sa = sin(a);
  vec2 rd = vec2(d.x * ca - d.y * sa, d.x * sa + d.y * ca);
  rd.x *= uResolution.y / uResolution.x;
  vec2 sUv = clamp(center + rd, vec2(0.0), vec2(1.0));
  vec4 warped = IMG_NORM_PIXEL(inputImage, sUv);
  vec4 src = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(mix(src.rgb, warped.rgb, uMix), src.a);
}
