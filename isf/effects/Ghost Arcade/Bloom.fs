/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Soft glow around bright areas. Adapted from Ghost Arcade's Bloom effect.",
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
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.35
        },
        {
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "threshold",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.65
        },
        {
            "NAME": "kneeValue",
            "LABEL": "Knee",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "radiusValue",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "anamorphic",
            "LABEL": "Anamorphic",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        }
    ]
}*/

// Ported from Ghost Arcade (bloomHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uAmount (amount)
#define uIntensity (intensity)
#define uThreshold (threshold)
#define uKnee (kneeValue)
#define uRadius (radiusValue)
#define uAnamorphic (anamorphic)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)

vec3 thresholdKnee(vec3 col, float threshold, float knee) {
  float br = max(max(col.r, col.g), col.b);
  float kneeAmt = max(knee, 0.0001);
  float soft = clamp(br - threshold + kneeAmt, 0.0, 2.0 * kneeAmt);
  soft = soft * soft / (4.0 * kneeAmt + 0.00001);
  float contribution = max(soft, br - threshold) / max(br, 0.00001);
  return col * contribution;
}

vec3 ringSample(sampler2D tex, vec2 uv, vec2 px, float radius) {
  vec3 acc = vec3(0.0);
  float aniso = clamp(uAnamorphic, 0.0, 1.0);
  vec2 r = px * radius * vec2(1.0, 1.0 - aniso * 0.92);
  acc += texture2D(tex, uv + r * vec2( 1.0,  0.0)).rgb;
  acc += texture2D(tex, uv + r * vec2(-1.0,  0.0)).rgb;
  acc += texture2D(tex, uv + r * vec2( 0.7,  0.7)).rgb;
  acc += texture2D(tex, uv + r * vec2(-0.7,  0.7)).rgb;
  acc += texture2D(tex, uv + r * vec2( 0.7, -0.7)).rgb;
  acc += texture2D(tex, uv + r * vec2(-0.7, -0.7)).rgb;
  acc += texture2D(tex, uv + r * vec2( 0.0,  1.0)).rgb;
  acc += texture2D(tex, uv + r * vec2( 0.0, -1.0)).rgb;
  acc += texture2D(tex, uv + r * vec2( 1.7,  0.0)).rgb;
  acc += texture2D(tex, uv + r * vec2(-1.7,  0.0)).rgb;
  acc += texture2D(tex, uv + r * vec2( 0.0,  1.7)).rgb;
  acc += texture2D(tex, uv + r * vec2( 0.0, -1.7)).rgb;
  acc += texture2D(tex, uv).rgb;
  return acc / 13.0;
}

void main() {
  vec4 baseColor = IMG_NORM_PIXEL(inputImage, vUv);
  vec2 px = 1.0 / uResolution;
  float baseR = uRadius * 9.0 + 1.5;
  vec3 ring1 = ringSample(inputImage, vUv, px, baseR * 1.0);
  vec3 ring2 = ringSample(inputImage, vUv, px, baseR * 2.2);
  vec3 ring3 = ringSample(inputImage, vUv, px, baseR * 4.5);
  vec3 blurred = ring1 * 0.55 + ring2 * 0.3 + ring3 * 0.15;
  vec3 bloom = thresholdKnee(blurred, uThreshold, uKnee);
  bloom *= uIntensity;
  bloom *= vec3(uTintR, uTintG, uTintB);
  vec3 composited = 1.0 - (1.0 - baseColor.rgb) * (1.0 - bloom);
  vec3 finalColor = mix(baseColor.rgb, composited, uAmount);
  gl_FragColor = vec4(finalColor, baseColor.a);
}
