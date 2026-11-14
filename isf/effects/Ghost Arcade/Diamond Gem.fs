/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Faceted diamond with refraction highlights, rainbow dispersion, and sparkle. Adapted from Ghost Arcade's Diamond Gem effect.",
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
            "LABEL": "Facets",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount3",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.15
        },
        {
            "NAME": "threshold",
            "LABEL": "Sparkle",
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

// Ported from Ghost Arcade (diamondGemShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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
  float facets = 4.0 + uAmount2 * 16.0;
  float rotSpeed = uAmount3 * 2.0;
  float sparkle = uThreshold;

  // Centered coords with aspect ratio
  vec2 p = (uv - 0.5) * 2.0;
  p.x *= uResolution.x / uResolution.y;

  float t = uTime * rotSpeed;

  // Diamond shape using analytical geometry (no raymarching)
  // Rotate the 2D point to spin the diamond
  float ca = cos(t), sa = sin(t);
  vec2 rp = vec2(p.x * ca - p.y * sa * 0.3, p.x * sa * 0.3 + p.y * ca);

  // Diamond silhouette: top and bottom cones
  float ax = abs(rp.x);
  float topHalf = step(rp.y, 0.0);   // 1.0 if in top half (y<0 = bottom of diamond)
  float botHalf = 1.0 - topHalf;

  // Diamond outline: narrower at top, wider at equator, pointed at bottom
  float topWidth = 0.6;   // half-width at crown
  float equatorY = -0.15; // equator position
  float crownY = -0.55;   // top of diamond
  float pointY = 0.75;    // bottom point

  // Compute diamond boundary
  float diamondDist = 1.0;
  // Above equator (crown facets)
  float tCrown = clamp((rp.y - equatorY) / (crownY - equatorY), 0.0, 1.0);
  float crownWidth = mix(topWidth, topWidth * 0.7, tCrown);
  // Below equator (pavilion - tapers to point)
  float tPav = clamp((rp.y - equatorY) / (pointY - equatorY), 0.0, 1.0);
  float pavWidth = mix(topWidth, 0.0, tPav);

  float halfWidth = mix(pavWidth, crownWidth, step(rp.y, equatorY));
  float inDiamond = step(ax, halfWidth) * step(crownY, rp.y) * step(rp.y, pointY);

  // Faceted normal based on quantized angle
  float angle = atan(rp.y - equatorY, rp.x);
  float qAngle = floor(angle * facets / TAU + 0.5) * TAU / facets;
  vec2 facetN2 = vec2(cos(qAngle), sin(qAngle));
  vec3 facetNormal = normalize(vec3(facetN2.x, facetN2.y, 0.6));

  // Texture sampling through refraction (always compute, use outside branches)
  vec3 refDir = vec3(facetN2 * 0.4, 1.0);
  vec2 texUV = uv + refDir.xy * 0.15;
  vec3 texCol = IMG_NORM_PIXEL(inputImage, clamp(texUV, 0.0, 1.0)).rgb;

  // Also sample_ with different offsets for RGB dispersion
  vec3 texR = IMG_NORM_PIXEL(inputImage, clamp(uv + refDir.xy * 0.20, 0.0, 1.0)).rgb;
  vec3 texB = IMG_NORM_PIXEL(inputImage, clamp(uv + refDir.xy * 0.10, 0.0, 1.0)).rgb;
  vec3 dispersed = vec3(texR.r, texCol.g, texB.b);

  // Lighting
  vec3 lightDir = normalize(vec3(0.5, -0.8, 1.0));
  float diff = max(dot(facetNormal, lightDir), 0.0);
  vec3 viewDir = vec3(0.0, 0.0, 1.0);
  vec3 halfV = normalize(lightDir + viewDir);
  float spec = pow(max(dot(facetNormal, halfV), 0.0), 80.0);

  // Fresnel
  float fresnel = pow(1.0 - max(dot(viewDir, facetNormal), 0.0), 3.0);

  // Rainbow caustics
  float causticAngle = angle + t * 0.5;
  vec3 rainbow = 0.5 + 0.5 * cos(TAU * (causticAngle / TAU + vec3(0.0, 0.33, 0.67)));

  // Compose diamond
  vec3 gemCol = dispersed * (diff * 0.6 + 0.4);
  gemCol += rainbow * fresnel * 0.4;
  gemCol += vec3(spec) * sparkle * 3.0;
  gemCol += rainbow * spec * sparkle * 0.5;

  // Edge highlight
  float edgeDist = abs(ax - halfWidth * 0.97);
  float edge = smoothstep(0.02, 0.0, edgeDist) * inDiamond;
  gemCol += vec3(0.8, 0.85, 1.0) * edge * 0.5;

  // Mix with background
  vec3 col = mix(src.rgb * 0.15, gemCol, inDiamond);
  col = mix(src.rgb, col, blend);

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
