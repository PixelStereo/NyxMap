/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Directional emboss with colored lighting. Adapted from Ghost Arcade's Emboss Relight effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Atmosphere",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "strength",
            "LABEL": "Strength",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "angle",
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
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "detail",
            "LABEL": "Detail",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "specular",
            "LABEL": "Specular",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "colorPreserve",
            "LABEL": "Color Preserve",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "ambient",
            "LABEL": "Ambient",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        }
    ]
}*/

// Ported from Ghost Arcade (embossRelightHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uStrength (strength)
#define uAngle (angle)
#define uHeight (heightValue)
#define uDetail (detail)
#define uSpecular (specular)
#define uColorPreserve (colorPreserve)
#define uAmbient (ambient)

      // 0-3
         // 0-360
        // 0-4
        // 0-2 sample_ radius
      // 0-1
 // 0-1
       // 0-1 base brightness

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  vec2 texel = 1.0 / uResolution;
  float d = max(1.0, uDetail);

  // Build height field from high-pass filter_
  vec3 c = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec3 ll = IMG_NORM_PIXEL(inputImage, vUv + texel * vec2(-d, 0)).rgb;
  vec3 rr = IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( d, 0)).rgb;
  vec3 tt = IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 0, d)).rgb;
  vec3 bb = IMG_NORM_PIXEL(inputImage, vUv + texel * vec2( 0,-d)).rgb;

  float h0 = luma(c);
  float gx = (luma(rr) - luma(ll)) * uHeight;
  float gy = (luma(tt) - luma(bb)) * uHeight;

  // Surface normal
  vec3 N = normalize(vec3(-gx, -gy, 1.0));
  float ang = radians(uAngle);
  vec3 L = normalize(vec3(cos(ang), sin(ang), 0.7));
  float diff = max(0.0, dot(N, L));
  vec3 V = vec3(0.0, 0.0, 1.0);
  vec3 H = normalize(L + V);
  float spec = pow(max(0.0, dot(N, H)), 32.0) * uSpecular;

  float lit = uAmbient + diff * uStrength + spec;
  vec3 surfaceColor = mix(vec3(h0), c, uColorPreserve);
  vec3 result = surfaceColor * lit;
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
