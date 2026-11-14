/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Dissolve image into 3D particles forming sphere, cube, pyramid, torus and more. Adapted from Ghost Arcade's Explode 3D effect.",
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
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (explode3DShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float scatter = uAmount;
  float shapeId = floor(uAmount2 * 5.99);
  float spinSpeed = uAmount3 * 2.0;
  float bright = 0.5 + uThreshold;
  float camAngle = uAngle + uTime * spinSpeed;

  float gridRes = mix(25.0, 70.0, 1.0 - uAmount2 * 0.3);
  vec2 cellCount = vec2(gridRes, gridRes * uResolution.y / uResolution.x);
  vec2 cellSize = 1.0 / cellCount;

  vec3 finalColor = vec3(0.0);
  float totalWeight = 0.0;

  // Expanded search radius for 3D projection
  for (int ox = -3; ox <= 3; ox++) {
    for (int oy = -3; oy <= 3; oy++) {
      vec2 cellId = floor(uv * cellCount) + vec2(float(ox), float(oy));
      if (cellId.x < 0.0 || cellId.y < 0.0 || cellId.x >= cellCount.x || cellId.y >= cellCount.y) continue;

      vec2 cellCenter = (cellId + 0.5) * cellSize;
      vec4 cellColor = IMG_NORM_PIXEL(inputImage, cellCenter);

      float rnd1 = hash(cellId);
      float rnd2 = hash(cellId + 137.0);
      float rnd3 = hash(cellId + 271.0);

      float theta = cellCenter.x * TAU;
      float phi = cellCenter.y * PI;
      vec3 shapePos;

      if (shapeId < 1.0) {
        shapePos = vec3(sin(phi)*cos(theta), cos(phi), sin(phi)*sin(theta));
      } else if (shapeId < 2.0) {
        float face = floor(rnd1 * 6.0);
        vec3 p = vec3(cellCenter.x * 2.0 - 1.0, cellCenter.y * 2.0 - 1.0, 1.0);
        if (face < 1.0) shapePos = p;
        else if (face < 2.0) shapePos = vec3(-p.z, p.y, p.x);
        else if (face < 3.0) shapePos = vec3(p.z, p.y, -p.x);
        else if (face < 4.0) shapePos = vec3(p.x, p.z, -p.y);
        else if (face < 5.0) shapePos = vec3(p.x, -p.z, p.y);
        else shapePos = vec3(p.x, p.y, -p.z);
        shapePos = normalize(shapePos);
      } else if (shapeId < 3.0) {
        float h = cellCenter.y;
        float r = (1.0 - h) * 0.8;
        float a = cellCenter.x * TAU;
        float sides = floor(a / (TAU/4.0)) * (TAU/4.0) + TAU/8.0;
        shapePos = vec3(cos(sides)*r, h*2.0-1.0, sin(sides)*r);
      } else if (shapeId < 4.0) {
        float R = 0.7, r2 = 0.3;
        shapePos = vec3((R + r2*cos(phi))*cos(theta), r2*sin(phi), (R + r2*cos(phi))*sin(theta));
      } else if (shapeId < 5.0) {
        float r2 = 0.6;
        shapePos = vec3(r2*cos(theta), cellCenter.y*2.0-1.0, r2*sin(theta));
      } else {
        float t = cellCenter.y * 4.0 * PI;
        float r2 = 0.5;
        shapePos = vec3(r2*cos(t + theta), cellCenter.y*2.0-1.0, r2*sin(t + theta));
      }

      // Rotate shape
      float ca = cos(camAngle), sa = sin(camAngle);
      shapePos = vec3(shapePos.x*ca - shapePos.z*sa, shapePos.y, shapePos.x*sa + shapePos.z*ca);

      // Scatter
      vec3 scatterDir = normalize(shapePos + vec3(rnd1-0.5, rnd2-0.5, rnd3-0.5)*0.5);
      vec3 pos3d = shapePos + scatterDir * scatter * (0.5 + rnd1) * 1.5;

      // Project to 2D
      float z = pos3d.z + 3.0;
      vec2 proj = pos3d.xy / z * 0.5 + 0.5;
      float pSize = cellSize.x * (1.0 + uAmount2) / max(z * 0.4, 0.1);

      vec2 diff = (uv - proj) / vec2(1.0, uResolution.x / uResolution.y);
      float dist = length(diff);
      float particle = smoothstep(pSize, pSize * 0.2, dist);

      if (particle > 0.01) {
        vec3 lightDir = normalize(vec3(0.5, 0.7, 1.0));
        float diffuse = max(dot(normalize(shapePos), lightDir), 0.0) * 0.7 + 0.3;
        vec3 lit = cellColor.rgb * diffuse * bright * uColor;
        float depthFade = 1.0 / (1.0 + max(z - 2.0, 0.0) * 0.3);
        finalColor += lit * particle * depthFade;
        totalWeight += particle * depthFade;
      }
    }
  }

  if (totalWeight > 0.01) {
    finalColor /= totalWeight;
    gl_FragColor = vec4(mix(src.rgb * (1.0 - scatter * 0.5), finalColor, clamp(totalWeight, 0.0, 1.0)), src.a);
  } else {
    gl_FragColor = vec4(src.rgb * (1.0 - scatter * 0.5), src.a);
  }
}
