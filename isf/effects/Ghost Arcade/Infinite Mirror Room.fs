/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Image on walls of a mirrored box with infinite reflections receding into depth. Adapted from Ghost Arcade's Infinite Mirror Room effect.",
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
            "LABEL": "Room Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount3",
            "LABEL": "Camera Move",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.15
        },
        {
            "NAME": "threshold",
            "LABEL": "Reflection Fade",
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

// Ported from Ghost Arcade (mirrorRoomShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float blend = uAmount;
  float roomSize = 0.5 + uAmount2 * 2.0;
  float camSpeed = uAmount3 * 0.5;
  float reflFade = 0.3 + uThreshold * 0.65;

  vec2 p = (uv - 0.5) * 2.0;
  p.x *= uResolution.x / uResolution.y;

  float t = uTime;

  // Camera inside the box, gently moving
  vec3 ro = vec3(
    sin(t * camSpeed * 0.7) * roomSize * 0.3,
    sin(t * camSpeed * 0.5) * roomSize * 0.2,
    cos(t * camSpeed * 0.6) * roomSize * 0.3
  );

  // Look direction slowly rotating
  float lookAngle = t * camSpeed * 0.4;
  vec3 lookDir = vec3(cos(lookAngle), sin(t * camSpeed * 0.2) * 0.3, sin(lookAngle));
  vec3 fwd = normalize(lookDir);
  vec3 right = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));
  vec3 up = cross(right, fwd);
  vec3 rd = normalize(fwd + p.x * right * 0.8 + p.y * up * 0.8);

  // Box bounds
  vec3 boxMin = vec3(-roomSize);
  vec3 boxMax = vec3(roomSize);

  vec3 col = vec3(0.0);
  float energy = 1.0;

  vec3 rayPos = ro;
  vec3 rayDir = rd;

  // Bounce reflections
  for (int bounce = 0; bounce < 8; bounce++) {
    // Ray-box intersection (from inside)
    vec3 tMin = (boxMin - rayPos) / rayDir;
    vec3 tMax = (boxMax - rayPos) / rayDir;
    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);

    float tFar = min(min(t2.x, t2.y), t2.z);

    // We want the first positive intersection
    float hitT = tFar;
    if (hitT < 0.001) hitT = 0.001;

    // Find which face we hit
    vec3 hitPos = rayPos + rayDir * hitT;
    vec3 absHit = abs(hitPos);
    vec3 normal;
    vec2 faceUV;

    if (absHit.x >= absHit.y - 0.001 && absHit.x >= absHit.z - 0.001) {
      normal = vec3(-sign(rayDir.x), 0.0, 0.0);
      faceUV = hitPos.yz / roomSize * 0.5 + 0.5;
    } else if (absHit.y >= absHit.z - 0.001) {
      normal = vec3(0.0, -sign(rayDir.y), 0.0);
      faceUV = hitPos.xz / roomSize * 0.5 + 0.5;
    } else {
      normal = vec3(0.0, 0.0, -sign(rayDir.z));
      faceUV = hitPos.xy / roomSize * 0.5 + 0.5;
    }

    // Sample texture on this wall
    vec3 texCol = IMG_NORM_PIXEL(inputImage, clamp(faceUV, 0.0, 1.0)).rgb;

    // Add contribution
    col += texCol * energy * 0.35;

    // Fade energy per bounce
    energy *= reflFade;
    if (energy < 0.01) break;

    // Reflect
    rayPos = hitPos + normal * 0.01;
    rayDir = reflect(rayDir, normal);
  }

  col = mix(src.rgb, col, blend);
  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
