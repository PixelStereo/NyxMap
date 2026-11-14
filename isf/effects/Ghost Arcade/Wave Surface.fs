/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Image stretched across animated ocean waves with specular lighting. Adapted from Ghost Arcade's Wave Surface effect.",
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
            "NAME": "mode",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "amount",
            "LABEL": "Amplitude",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount2",
            "LABEL": "Frequency",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount3",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "threshold",
            "LABEL": "Specular",
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
        }
    ]
}*/

// Ported from Ghost Arcade (waveSurfaceShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(mode + 0.5))
#define uAmount (amount)
#define uAmount2 (amount2)
#define uAmount3 (amount3)
#define uThreshold (threshold)
#define uAngle (angle)
#define uCenter vec2(0.5, 0.5)
#define uColor vec3(0.5, 0.5, 0.5)

#define PI 3.14159265359
#define TAU 6.28318530718

float waveHeight(vec2 p, float t, float amp, float freq) {
  float h = 0.0;
  h += sin(p.x * freq + t * 1.3) * 0.5;
  h += sin(p.y * freq * 0.7 + t * 0.9) * 0.3;
  h += sin((p.x + p.y) * freq * 0.5 + t * 1.7) * 0.2;
  h += sin((p.x - p.y) * freq * 0.8 + t * 2.1) * 0.15;
  h += sin(p.x * freq * 2.0 + p.y * freq * 1.5 + t * 2.5) * 0.1;
  return h * amp;
}

vec3 waveNormal(vec2 p, float t, float amp, float freq) {
  float eps = 0.01;
  float hL = waveHeight(p - vec2(eps, 0.0), t, amp, freq);
  float hR = waveHeight(p + vec2(eps, 0.0), t, amp, freq);
  float hD = waveHeight(p - vec2(0.0, eps), t, amp, freq);
  float hU = waveHeight(p + vec2(0.0, eps), t, amp, freq);
  return normalize(vec3(hL - hR, 2.0 * eps, hD - hU));
}

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float amp = 0.05 + uAmount * 0.3;
  float freq = 2.0 + uAmount2 * 10.0;
  float speed = 0.5 + uAmount3 * 3.0;
  float specular = uThreshold;

  float t = uTime * speed;

  vec2 p = (uv - 0.5) * 2.0;
  p.x *= uResolution.x / uResolution.y;

  // Camera looking down at the wave surface
  vec3 ro = vec3(0.0, 2.0, -1.5);
  vec3 rd = normalize(vec3(p.x, -1.2, p.y + 0.5));

  // Intersect with approximate plane y = 0
  float tPlane = -ro.y / rd.y;
  vec3 col = vec3(0.01, 0.02, 0.05);

  if (tPlane > 0.0) {
    vec3 hitPos = ro + rd * tPlane;
    vec2 surfaceUv = hitPos.xz;

    // Wave displacement
    float h = waveHeight(surfaceUv, t, amp, freq);
    vec3 normal = waveNormal(surfaceUv, t, amp, freq);

    // Refracted texture lookup
    vec3 refr = refract(rd, normal, 0.75);
    vec2 texUV = surfaceUv * 0.3 + 0.5 + refr.xz * 0.1;
    texUV = fract(texUV);

    vec3 texCol = IMG_NORM_PIXEL(inputImage, texUV).rgb;

    // Lighting
    vec3 lightDir = normalize(vec3(0.3, 1.0, -0.5));
    float diff = max(dot(normal, lightDir), 0.0) * 0.6 + 0.4;

    // Specular
    vec3 viewDir = normalize(-rd);
    vec3 reflDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflDir), 0.0), 32.0) * specular;

    // Fresnel
    float fresnel = pow(1.0 - max(dot(viewDir, normal), 0.0), 4.0) * 0.3;

    col = texCol * diff + vec3(0.4, 0.6, 0.9) * fresnel + vec3(spec);

    // Depth fog
    float fog = exp(-tPlane * 0.15);
    col *= fog;
  }

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
