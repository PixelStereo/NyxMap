/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Wrap image around a rotating 3D cylinder with perspective and lighting. Adapted from Ghost Arcade's Cylinder Wrap effect.",
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
            "LABEL": "Blend",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.8
        },
        {
            "NAME": "amount2",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount3",
            "LABEL": "Spin Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "threshold",
            "LABEL": "Perspective",
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

// Ported from Ghost Arcade (cylinderWrapShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

  float blend = uAmount;
  float radius = 0.2 + uAmount2 * 0.8;
  float spinSpeed = uAmount3 * 2.0;
  float perspective = 0.5 + uThreshold * 2.0;

  // Center UV
  vec2 p = (uv - 0.5) * 2.0;
  p.x *= uResolution.x / uResolution.y;

  // Cylinder rotation angle
  float angle = uTime * spinSpeed;

  // Ray from camera
  vec3 ro = vec3(0.0, 0.0, -2.0 - perspective);
  vec3 rd = normalize(vec3(p, 2.0));

  // Cylinder along Y axis at origin
  // Ray-cylinder intersection: (ro.x + t*rd.x)^2 + (ro.z + t*rd.z)^2 = r^2
  float a = rd.x * rd.x + rd.z * rd.z;
  float b = 2.0 * (ro.x * rd.x + ro.z * rd.z);
  float c = ro.x * ro.x + ro.z * ro.z - radius * radius;
  float disc = b * b - 4.0 * a * c;

  vec3 col = src.rgb * 0.1;

  if (disc > 0.0) {
    float t = (-b - sqrt(disc)) / (2.0 * a);
    if (t > 0.0) {
      vec3 hit = ro + t * rd;

      // Check if within cylinder height
      if (abs(hit.y) < 1.0) {
        // Map to texture UV
        float theta = atan(hit.x, hit.z) + angle;
        float texU = fract(theta / TAU);
        float texV = hit.y * 0.5 + 0.5;

        vec3 texCol = IMG_NORM_PIXEL(inputImage, vec2(texU, texV)).rgb;

        // Lighting
        vec3 normal = normalize(vec3(hit.x, 0.0, hit.z));
        vec3 lightDir = normalize(vec3(0.5, 0.7, -1.0));
        float diff = max(dot(normal, lightDir), 0.0) * 0.6 + 0.4;

        // Specular
        vec3 viewDir = normalize(-rd);
        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.5;

        col = texCol * diff + vec3(spec);
      }
    }
  }

  col = mix(src.rgb, col, blend);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
