/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Convert image to 3D voxel grid where brightness drives cube height. Adapted from Ghost Arcade's Voxel Displace effect.",
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
            "LABEL": "Depth Scale",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Resolution",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
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
            "LABEL": "Gap Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
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

// Ported from Ghost Arcade (voxelDisplaceShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
  vec2 uv = vUv;
  vec4 src = IMG_NORM_PIXEL(inputImage, uv);

  float depthScale = 0.3 + uAmount * 1.5;
  float gridRes = floor(mix(6.0, 32.0, uAmount2));
  float rotAngle = uTime * uAmount3 * 0.8;
  float gap = uThreshold * 0.3;

  // Isometric-style projection: tilt the grid
  vec2 p = (uv - 0.5) * 2.0;
  p.x *= uResolution.x / uResolution.y;

  // Apply rotation
  float ca = cos(rotAngle), sa = sin(rotAngle);
  vec2 rp = vec2(p.x * ca - p.y * sa, p.x * sa + p.y * ca);

  // Isometric tilt: compress Y to simulate looking down at an angle
  float tiltAngle = 0.65; // ~37 degrees
  vec2 isoP = vec2(rp.x, rp.y / tiltAngle);

  // Map to grid coordinates
  vec2 gridUV = isoP * 0.5 + 0.5;

  // Quantize to voxel grid
  vec2 voxelID = floor(gridUV * gridRes);
  vec2 cellUV = fract(gridUV * gridRes); // position within cell 0..1

  // Texture coordinate for this voxel
  vec2 texCoord = (voxelID + 0.5) / gridRes;

  // Sample texture (always, outside any branches)
  vec3 texCol = IMG_NORM_PIXEL(inputImage, clamp(texCoord, 0.0, 1.0)).rgb;
  float height = luma(texCol) * depthScale;

  // Check if we're inside the grid
  vec3 col = vec3(0.02, 0.02, 0.04);

  float inGrid = step(0.0, gridUV.x) * step(gridUV.x, 1.0) * step(0.0, gridUV.y) * step(gridUV.y, 1.0);

  // Gap between voxels
  float halfGap = gap * 0.5;
  float inCell = step(halfGap, cellUV.x) * step(cellUV.x, 1.0 - halfGap) *
                 step(halfGap, cellUV.y) * step(cellUV.y, 1.0 - halfGap);

  // Simulate 3D block: the cell's vertical offset shifts it up
  // The "side" of the block is visible when neighboring cells are shorter
  // We fake the 3D look by using height to shift the cell and color the faces

  // Top face color (lit from above-left)
  vec3 topCol = texCol * 1.1;

  // Side faces: check neighbors for depth
  vec2 leftTexCoord = (voxelID + vec2(-0.5, 0.5)) / gridRes;
  vec2 frontTexCoord = (voxelID + vec2(0.5, 1.5)) / gridRes;
  vec3 leftTex = IMG_NORM_PIXEL(inputImage, clamp(leftTexCoord, 0.0, 1.0)).rgb;
  vec3 frontTex = IMG_NORM_PIXEL(inputImage, clamp(frontTexCoord, 0.0, 1.0)).rgb;
  float leftH = luma(leftTex) * depthScale;
  float frontH = luma(frontTex) * depthScale;

  // Height difference creates visible side faces
  float sideExposureX = max(height - leftH, 0.0);
  float sideExposureY = max(height - frontH, 0.0);

  // Isometric pixel offset based on height
  float pixelH = height * 0.15;

  // Determine which face we're seeing
  // The "top" of the voxel block
  float topFace = inCell;

  // Right-side face (visible when left neighbor is shorter)
  float rightSide = step(0.0, sideExposureX) * step(cellUV.x, halfGap * 3.0) * (1.0 - step(halfGap, cellUV.x));
  // Front face (visible when front neighbor is shorter)
  float frontSide = step(0.0, sideExposureY) * step(1.0 - halfGap * 3.0, cellUV.y) * step(cellUV.y, 1.0);

  // Light direction simulation
  vec3 rightCol = texCol * 0.6;  // darker side
  vec3 frontCol = texCol * 0.4;  // darkest side

  // Compose the voxel
  vec3 voxelCol = topCol * topFace + rightCol * rightSide + frontCol * frontSide;

  // Apply height as vertical UV offset for parallax effect
  vec2 shiftedUV = uv + vec2(0.0, pixelH);
  vec3 shiftedSrc = IMG_NORM_PIXEL(inputImage, clamp(shiftedUV, 0.0, 1.0)).rgb;

  // Final: blend voxelized look with height-shifted source
  float showVoxel = inGrid * max(topFace, max(rightSide, frontSide));
  col = mix(vec3(0.02), voxelCol, showVoxel);

  // Add subtle ambient occlusion at gaps
  float ao = smoothstep(0.0, halfGap * 2.0, min(cellUV.x, min(cellUV.y, min(1.0 - cellUV.x, 1.0 - cellUV.y))));
  col *= mix(0.7, 1.0, ao);

  // Add height-based shadow
  col *= 0.6 + height * 0.6;

  gl_FragColor = vec4(clamp(col, 0.0, 1.0), src.a);
}
