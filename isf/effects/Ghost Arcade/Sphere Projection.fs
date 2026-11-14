/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Project image onto a rotating lit 3D sphere with Fresnel rim glow. Adapted from Ghost Arcade's Sphere Projection effect.",
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
            "NAME": "angleValue",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (sphereProjectShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(mode + 0.5))
#define uAmount (amount)
#define uAmount2 (amount2)
#define uAmount3 (amount3)
#define uThreshold (threshold)
#define uAngle (angleValue)
#define uCenter vec2(0.5, 0.5)
#define uColor vec3(0.5, 0.5, 0.5)

#define PI 3.14159265359
#define TAU 6.28318530718

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);
  vec4 outColor = src;

  float blend = uAmount;
  float roughness = uAmount2;
  float spinSpd = uAmount3 * 2.0;
  float rimGlow = uThreshold;

  vec2 centered = (uv - 0.5) * vec2(uResolution.x / uResolution.y, 1.0);
  float r = length(centered);
  float sphereR = 0.45;

  if (r < sphereR) {
    float z = sqrt(sphereR * sphereR - r * r);
    vec3 normal = normalize(vec3(centered, z));

    float angle = uTime * spinSpd;
    float ca = cos(angle), sa = sin(angle);
    vec3 rotN = vec3(normal.x * ca - normal.z * sa, normal.y, normal.x * sa + normal.z * ca);

    vec2 sphereUV = vec2(atan(rotN.z, rotN.x) / TAU + 0.5, asin(clamp(rotN.y, -1.0, 1.0)) / PI + 0.5);
    vec3 texColor = IMG_NORM_PIXEL(inputImage, sphereUV).rgb;

    vec3 lightDir = normalize(vec3(0.5, 0.7, 1.0));
    float diff = max(dot(normal, lightDir), 0.0);
    float spec = pow(max(dot(reflect(-lightDir, normal), vec3(0,0,1)), 0.0), mix(4.0, 64.0, roughness));
    float fresnel = pow(1.0 - abs(normal.z), 3.0) * rimGlow;

    vec3 lit = texColor * (diff * 0.7 + 0.3) * uColor + vec3(spec * 0.3) + vec3(fresnel) * uColor;
    outColor.rgb = mix(src.rgb, lit, blend);
  } else {
    float edgeFade = smoothstep(sphereR + 0.02, sphereR, r);
    outColor.rgb = mix(src.rgb, src.rgb * 0.3, edgeFade * blend * 0.5);
  }

  gl_FragColor = vec4(clamp(outColor.rgb, 0.0, 1.0), clamp(outColor.a, 0.0, 1.0));
}
