/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Paper-fold creases across the image with 3D depth shading and animation. Adapted from Ghost Arcade's Origami Fold effect.",
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
            "LABEL": "Fold Depth",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Fold Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount3",
            "LABEL": "Animation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        },
        {
            "NAME": "threshold",
            "LABEL": "Sharpness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.6
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

// Ported from Ghost Arcade (origamiFoldShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

  float foldDepth = 0.1 + uAmount * 0.9;
  float foldCount = 2.0 + floor(uAmount2 * 10.0);
  float speed = 0.5 + uAmount3 * 2.0;
  float creaseSharpen = 2.0 + uThreshold * 20.0;

  float t = uTime * speed;

  // Animated fold intensity
  float foldAnim = (sin(t) * 0.5 + 0.5) * foldDepth;

  // Horizontal folds
  float hFold = uv.y * foldCount;
  float hFoldId = floor(hFold);
  float hFoldFrac = fract(hFold);

  // Vertical folds
  float vFold = uv.x * foldCount;
  float vFoldId = floor(vFold);
  float vFoldFrac = fract(vFold);

  // Triangle fold: fold direction alternates
  float hDir = mod(hFoldId, 2.0) * 2.0 - 1.0;
  float vDir = mod(vFoldId, 2.0) * 2.0 - 1.0;

  // Fold angle — creates a V shape within each strip
  float hAngle = (hFoldFrac - 0.5) * 2.0 * hDir;
  float vAngle = (vFoldFrac - 0.5) * 2.0 * vDir;

  // Combined fold normal (simplified 3D fold)
  float foldNormalZ = cos(hAngle * foldAnim * PI * 0.5) * cos(vAngle * foldAnim * PI * 0.5);
  float foldNormalX = sin(vAngle * foldAnim * PI * 0.5) * 0.5;
  float foldNormalY = sin(hAngle * foldAnim * PI * 0.5) * 0.5;
  vec3 normal = normalize(vec3(foldNormalX, foldNormalY, foldNormalZ));

  // Lighting
  vec3 lightDir = normalize(vec3(0.5, 0.7, 1.0));
  float diff = max(dot(normal, lightDir), 0.0) * 0.6 + 0.4;

  // Specular for paper sheen
  vec3 viewDir = vec3(0.0, 0.0, 1.0);
  vec3 halfDir = normalize(lightDir + viewDir);
  float spec = pow(max(dot(normal, halfDir), 0.0), 16.0) * 0.3;

  // UV displacement from fold
  vec2 dispUv = uv;
  dispUv.x += foldNormalX * foldAnim * 0.02;
  dispUv.y += foldNormalY * foldAnim * 0.02;

  vec3 texCol = IMG_NORM_PIXEL(inputImage, clamp(dispUv, 0.0, 1.0)).rgb;

  // Crease darkening
  float hCrease = pow(abs(hFoldFrac - 0.5) * 2.0, creaseSharpen);
  float vCrease = pow(abs(vFoldFrac - 0.5) * 2.0, creaseSharpen);
  float crease = min(hCrease, vCrease);
  float creaseDark = mix(0.6, 1.0, crease);

  // Shadow on folded parts
  float shadow = mix(1.0, foldNormalZ, foldAnim);

  vec3 col = texCol * diff * creaseDark * shadow + vec3(spec) * foldAnim;

  // Paper edge highlight at creases
  float edgeDist = min(abs(hFoldFrac - 0.5), abs(vFoldFrac - 0.5));
  float edgeLine = smoothstep(0.02, 0.0, edgeDist) * foldAnim * 0.4;
  col += vec3(edgeLine);

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
