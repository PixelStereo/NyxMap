/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Wrap image onto faces of a rotating 3D cube with edge glow. Adapted from Ghost Arcade's Cube Projection effect.",
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

// Ported from Ghost Arcade (cubeProjectShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

mat2 rot2d(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);
  vec4 outColor = src;

  float cubeSize = 0.3 + uAmount * 0.5;
  float edgeGlow = uAmount2;
  float spinSpd = uAmount3 * 1.5;
  float manualRot = uAngle;

  vec2 centered = (uv - 0.5) * vec2(uResolution.x / uResolution.y, 1.0);
  vec3 ro = vec3(0.0, 0.0, -2.5);
  vec3 rd = normalize(vec3(centered, 1.0));

  float angle = uTime * spinSpd + manualRot;
  float tilt = 0.4;

  rd.xz = rot2d(angle) * rd.xz;
  ro.xz = rot2d(angle) * ro.xz;
  rd.yz = rot2d(tilt) * rd.yz;
  ro.yz = rot2d(tilt) * ro.yz;

  // Ray-AABB intersection
  vec3 boxMin = vec3(-cubeSize);
  vec3 boxMax = vec3(cubeSize);
  vec3 invRd = 1.0 / rd;
  vec3 t1 = (boxMin - ro) * invRd;
  vec3 t2 = (boxMax - ro) * invRd;
  vec3 tMin = min(t1, t2);
  vec3 tMax = max(t1, t2);
  float tNear = max(max(tMin.x, tMin.y), tMin.z);
  float tFar = min(min(tMax.x, tMax.y), tMax.z);

  if (tNear < tFar && tFar > 0.0) {
    float t = tNear > 0.0 ? tNear : tFar;
    vec3 hitPos = ro + rd * t;
    vec3 absHit = abs(hitPos);

    vec2 faceUV;
    vec3 normal;
    if (absHit.x > absHit.y - 0.001 && absHit.x > absHit.z - 0.001) {
      faceUV = hitPos.yz / cubeSize * 0.5 + 0.5;
      normal = vec3(sign(hitPos.x), 0.0, 0.0);
    } else if (absHit.y > absHit.z - 0.001) {
      faceUV = hitPos.xz / cubeSize * 0.5 + 0.5;
      normal = vec3(0.0, sign(hitPos.y), 0.0);
    } else {
      faceUV = hitPos.xy / cubeSize * 0.5 + 0.5;
      normal = vec3(0.0, 0.0, sign(hitPos.z));
    }

    vec3 texColor = IMG_NORM_PIXEL(inputImage, clamp(faceUV, 0.0, 1.0)).rgb;
    vec3 lightDir = normalize(vec3(0.5, 0.7, 1.0));
    float diff = max(dot(normal, lightDir), 0.0) * 0.7 + 0.3;
    vec3 lit = texColor * diff * uColor;

    vec2 edgeDist = 1.0 - abs(faceUV * 2.0 - 1.0);
    float edgeMin = min(edgeDist.x, edgeDist.y);
    float edge = smoothstep(0.02, 0.08, edgeMin);
    lit += vec3(1.0 - edge) * edgeGlow * uColor;

    outColor.rgb = lit;
  } else {
    outColor.rgb = src.rgb * 0.15;
  }

  gl_FragColor = vec4(clamp(outColor.rgb, 0.0, 1.0), clamp(outColor.a, 0.0, 1.0));
}
