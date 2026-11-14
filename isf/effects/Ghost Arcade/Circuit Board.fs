/*{
    "ISFVSN": "2",
    "DESCRIPTION": "PCB-style traces and connection nodes from image luminance. Adapted from Ghost Arcade's Circuit Board effect.",
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
            "LABEL": "Trace Density",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Trace Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount3",
            "LABEL": "Glow",
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

// Ported from Ghost Arcade (circuitBoardShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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
    // Grid density: 6 to 60 cells across
    float gridCount = mix(6.0, 60.0, uAmount);
    // Trace thickness in cell-space: 0.02 to 0.2
    float traceThick = mix(0.02, 0.2, uAmount2);
    // Glow intensity
    float glowInt = mix(0.0, 2.0, uAmount3);

    vec2 uv = vUv;
    vec4 src = IMG_NORM_PIXEL(inputImage, uv);
    float srcLuma = luma(src.rgb);

    vec2 cellSize = vec2(1.0) / gridCount;
    vec2 cellIndex = floor(uv / cellSize);
    vec2 cellPos = fract(uv / cellSize);

    // Hash for this cell determines trace directions
    float h = hash(cellIndex);
    float h2 = hash(cellIndex + 73.0);
    float h3 = hash(cellIndex + 137.0);

    // Sample source at cell center
    vec2 cellCenterUV = (cellIndex + 0.5) * cellSize;
    vec4 cellSrc = IMG_NORM_PIXEL(inputImage, cellCenterUV);
    float cellLuma = luma(cellSrc.rgb);

    // Trace presence based on luminance
    float tracePresence = smoothstep(uThreshold - 0.1, uThreshold + 0.1, cellLuma);

    // Draw traces: horizontal and vertical lines through cell
    float traceMask = 0.0;

    // Horizontal trace
    if (h > 0.3) {
        float dy = abs(cellPos.y - 0.5);
        traceMask = max(traceMask, 1.0 - smoothstep(traceThick * 0.5 - 0.005, traceThick * 0.5 + 0.005, dy));
    }
    // Vertical trace
    if (h2 > 0.3) {
        float dx = abs(cellPos.x - 0.5);
        traceMask = max(traceMask, 1.0 - smoothstep(traceThick * 0.5 - 0.005, traceThick * 0.5 + 0.005, dx));
    }

    // Node/pad at center
    float centerDist = length(cellPos - 0.5);
    float padRadius = traceThick * 1.2;
    float pad = 1.0 - smoothstep(padRadius - 0.01, padRadius + 0.01, centerDist);
    traceMask = max(traceMask, pad);

    // Corner connections: L-shaped turns
    if (h3 > 0.5) {
        float corner1 = abs(cellPos.x - 0.5) + abs(cellPos.y - 0.5);
        // Not a true L-bend, but suggests connectivity with a diagonal reject
    }

    traceMask *= tracePresence;

    // Glow effect: soft distance from traces
    float glowDist = 0.0;
    if (h > 0.3) {
        float dy = abs(cellPos.y - 0.5);
        glowDist = max(glowDist, exp(-dy * 8.0));
    }
    if (h2 > 0.3) {
        float dx = abs(cellPos.x - 0.5);
        glowDist = max(glowDist, exp(-dx * 8.0));
    }
    glowDist *= tracePresence;

    // PCB background color (dark green)
    vec3 pcbBg = vec3(0.0, 0.05, 0.02);
    // Trace color from source
    vec3 traceColor = mix(vec3(0.7, 0.85, 0.3), cellSrc.rgb, 0.5);
    // Glow color
    vec3 glowColor = traceColor * glowInt * 0.3;

    vec3 finalColor = pcbBg;
    finalColor += glowColor * glowDist;
    finalColor = mix(finalColor, traceColor, traceMask);

    gl_FragColor = vec4(finalColor, 1.0);
}
