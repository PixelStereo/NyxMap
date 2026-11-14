/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Voronoi cell tessellation with dark lead borders. Adapted from Ghost Arcade's Stained Glass effect.",
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
            "LABEL": "Cell Count",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Lead Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount3",
            "LABEL": "Saturation",
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

// Ported from Ghost Arcade (stainedGlassShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

vec2 hashVec2(vec2 p) {
    return vec2(hash(p), hash(p + vec2(37.0, 91.0)));
}

void main() {
    // Cell count: 5 to 50 across
    float cellCount = mix(5.0, 50.0, uAmount);
    // Lead thickness: 0.01 to 0.15 in cell-space
    float leadThick = mix(0.01, 0.15, uAmount2);
    // Saturation boost: 0 to 1.5
    float satBoost = mix(0.0, 1.5, uAmount3);

    vec2 uv = vUv;
    vec2 scaledUV = uv * cellCount;
    vec2 cellBase = floor(scaledUV);

    float minDist = 10.0;
    float secondDist = 10.0;
    vec2 closestCell = vec2(0.0);

    // Search 3x3 neighborhood
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = cellBase + vec2(float(x), float(y));
            vec2 point = neighbor + hashVec2(neighbor);
            float d = length(scaledUV - point);
            if (d < minDist) {
                secondDist = minDist;
                minDist = d;
                closestCell = neighbor;
            } else if (d < secondDist) {
                secondDist = d;
            }
        }
    }

    // Edge distance (difference between closest and second closest)
    float edge = secondDist - minDist;

    // Sample source at closest cell center
    vec2 cellCenter = (closestCell + hashVec2(closestCell)) / cellCount;
    cellCenter = clamp(cellCenter, 0.0, 1.0);
    vec4 src = IMG_NORM_PIXEL(inputImage, cellCenter);

    // Boost saturation
    float l = luma(src.rgb);
    vec3 saturated = mix(vec3(l), src.rgb, 1.0 + satBoost);
    saturated = max(saturated, 0.0);

    // Slight brightness boost for stained glass glow
    saturated *= 1.1;

    // Lead border
    float leadMask = 1.0 - smoothstep(leadThick - 0.02, leadThick + 0.02, edge);
    vec3 leadColor = vec3(0.02);

    vec3 finalColor = mix(saturated, leadColor, leadMask);

    gl_FragColor = vec4(finalColor, 1.0);
}
