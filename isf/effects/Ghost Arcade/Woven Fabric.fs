/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Interlocking warp and weft threads colored by source. Adapted from Ghost Arcade's Woven Fabric effect.",
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
            "LABEL": "Thread Density",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Thread Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount3",
            "LABEL": "Shadow Depth",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
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

// Ported from Ghost Arcade (wovenFabricShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    // Thread density: 10 to 80 threads across
    float threadCount = mix(10.0, 80.0, uAmount);
    // Thread width ratio within cell: 0.3 to 0.95
    float threadWidth = mix(0.3, 0.95, uAmount2);
    // Shadow depth: 0 to 0.6
    float shadowDepth = mix(0.0, 0.6, uAmount3);

    vec2 uv = vUv;
    vec2 scaled = uv * threadCount;
    vec2 cellIndex = floor(scaled);
    vec2 cellPos = fract(scaled);

    // Determine weave pattern: checkerboard for over/under
    float checker = mod(cellIndex.x + cellIndex.y, 2.0);

    // Thread masks
    float halfWidth = threadWidth * 0.5;

    // Warp thread (vertical): centered horizontally in cell
    float warpDist = abs(cellPos.x - 0.5);
    float warpMask = 1.0 - smoothstep(halfWidth - 0.02, halfWidth + 0.02, warpDist);

    // Weft thread (horizontal): centered vertically in cell
    float weftDist = abs(cellPos.y - 0.5);
    float weftMask = 1.0 - smoothstep(halfWidth - 0.02, halfWidth + 0.02, weftDist);

    // Sample source colors for each thread direction
    vec2 warpUV = vec2((cellIndex.x + 0.5) / threadCount, uv.y);
    vec2 weftUV = vec2(uv.x, (cellIndex.y + 0.5) / threadCount);
    warpUV = clamp(warpUV, 0.0, 1.0);
    weftUV = clamp(weftUV, 0.0, 1.0);

    vec3 warpColor = IMG_NORM_PIXEL(inputImage, warpUV).rgb;
    vec3 weftColor = IMG_NORM_PIXEL(inputImage, weftUV).rgb;

    // Thread shading: rounded cross-section
    float warpShade = 1.0 - warpDist * warpDist * 4.0;
    float weftShade = 1.0 - weftDist * weftDist * 4.0;
    warpShade = clamp(warpShade, 0.3, 1.0);
    weftShade = clamp(weftShade, 0.3, 1.0);

    warpColor *= warpShade;
    weftColor *= weftShade;

    // Over/under logic
    // checker == 0: warp is on top; checker == 1: weft is on top
    vec3 bgColor = vec3(0.03); // dark gap between threads
    vec3 result = bgColor;

    // Shadow on the thread that goes under
    float shadow = 1.0 - shadowDepth;

    if (checker < 0.5) {
        // Warp on top: draw weft first (underneath), then warp
        if (weftMask > 0.0) {
            result = mix(result, weftColor * shadow, weftMask);
        }
        if (warpMask > 0.0) {
            result = mix(result, warpColor, warpMask);
        }
    } else {
        // Weft on top: draw warp first (underneath), then weft
        if (warpMask > 0.0) {
            result = mix(result, warpColor * shadow, warpMask);
        }
        if (weftMask > 0.0) {
            result = mix(result, weftColor, weftMask);
        }
    }

    gl_FragColor = vec4(result, 1.0);
}
