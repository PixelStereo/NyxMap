/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Irregular shaped tiles with grout lines and random rotation. Adapted from Ghost Arcade's Mosaic Tile effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Advanced Text & Pattern",
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
            "LABEL": "Tile Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Grout Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "amount3",
            "LABEL": "Rotation",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
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

// Ported from Ghost Arcade (mosaicTileShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    // Tile size: 5 to 60 tiles across
    float tileCount = mix(5.0, 60.0, uAmount);
    // Grout width: 0.01 to 0.15 in tile-space
    float groutWidth = mix(0.01, 0.15, uAmount2);
    // Rotation randomness: 0 to 0.5 radians
    float rotRand = mix(0.0, 0.5, uAmount3);

    vec2 uv = vUv;
    float aspect = uResolution.x / uResolution.y;
    vec2 scaled = uv * vec2(tileCount * aspect, tileCount);

    // Offset every other row for brick-like pattern
    vec2 cellIndex = floor(scaled);
    float rowOffset = mod(cellIndex.y, 2.0) * 0.5;
    scaled.x += rowOffset;
    cellIndex = floor(scaled);
    vec2 cellPos = fract(scaled);

    // Random rotation per tile
    float angle = (hash(cellIndex) - 0.5) * 2.0 * rotRand;
    float ca = cos(angle);
    float sa = sin(angle);
    vec2 rotatedPos = vec2(
        ca * (cellPos.x - 0.5) - sa * (cellPos.y - 0.5) + 0.5,
        sa * (cellPos.x - 0.5) + ca * (cellPos.y - 0.5) + 0.5
    );

    // Random size variation per tile
    float sizeVar = 0.85 + 0.15 * hash(cellIndex + 41.0);

    // Tile shape: rounded rectangle with slight variation
    vec2 tileDist = abs(rotatedPos - 0.5) / (0.5 * sizeVar);
    // Superellipse-ish distance for more interesting tile shape
    float roundness = 0.08;
    float tileEdge = max(tileDist.x, tileDist.y);
    // Smooth rounded corners
    float cornerDist = length(max(tileDist - vec2(1.0 - roundness), 0.0));
    float tileMask = tileEdge + cornerDist * 0.5;

    // Grout: border region
    float groutHalf = groutWidth * 0.5;
    float groutMask = smoothstep(1.0 - groutHalf - 0.02, 1.0 - groutHalf, tileMask);

    // Sample source at tile center
    vec2 tileCenterScaled = cellIndex + 0.5;
    tileCenterScaled.x -= rowOffset;
    vec2 tileCenterUV = tileCenterScaled / vec2(tileCount * aspect, tileCount);
    tileCenterUV = clamp(tileCenterUV, 0.0, 1.0);
    vec4 src = IMG_NORM_PIXEL(inputImage, tileCenterUV);

    // Slight color variation per tile
    float colorVar = 0.9 + 0.1 * hash(cellIndex + 200.0);
    vec3 tileColor = src.rgb * colorVar;

    // Subtle shading: darken edges of tile
    float edgeShade = 1.0 - tileMask * 0.15;
    tileColor *= edgeShade;

    // Grout color
    vec3 groutColor = vec3(0.12, 0.11, 0.10);

    vec3 finalColor = mix(tileColor, groutColor, groutMask);

    gl_FragColor = vec4(finalColor, 1.0);
}
