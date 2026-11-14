/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Braille-like 2x4 dot grids where luminance activates dots. Adapted from Ghost Arcade's Braille Pattern effect.",
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
            "LABEL": "Cell Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount2",
            "LABEL": "Dot Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount3",
            "LABEL": "Color Mix",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "thresholdValue",
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

// Ported from Ghost Arcade (braillePatternShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(mode + 0.5))
#define uAmount (amount)
#define uAmount2 (amount2)
#define uAmount3 (amount3)
#define uThreshold (thresholdValue)
#define uAngle (angle)
#define uCenter vec2(0.5, 0.5)
#define uColor vec3(0.5, 0.5, 0.5)

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    // Cell size in pixels: range from 4 to 32 pixels
    float cellPixels = mix(4.0, 32.0, uAmount);
    // Dot radius ratio within subcell: 0.1 to 0.45
    float dotRadius = mix(0.1, 0.45, uAmount2);
    // Color mix: 0 = monochrome, 1 = full source color
    float colorMix = uAmount3;

    vec2 fragCoord = vUv * uResolution;

    // Braille cell is 2 columns x 4 rows of dots
    float cellW = cellPixels;
    float cellH = cellPixels * 2.0; // 4 rows vs 2 cols -> taller cell

    // Which cell are we in
    vec2 cellIndex = floor(fragCoord / vec2(cellW, cellH));
    // Position within cell, normalized 0..1
    vec2 cellPos = fract(fragCoord / vec2(cellW, cellH));

    // Subcell: 2 cols, 4 rows
    vec2 subIndex = floor(cellPos * vec2(2.0, 4.0));
    vec2 subPos = fract(cellPos * vec2(2.0, 4.0));

    // Sample source at center of each subcell
    vec2 subCenterUV = (cellIndex * vec2(cellW, cellH) + (subIndex + 0.5) * vec2(cellW / 2.0, cellH / 4.0)) / uResolution;
    subCenterUV = clamp(subCenterUV, 0.0, 1.0);
    vec4 subSample = IMG_NORM_PIXEL(inputImage, subCenterUV);
    float subLuma = luma(subSample.rgb);

    // Sample source at cell center for overall color
    vec2 cellCenterUV = (cellIndex + 0.5) * vec2(cellW, cellH) / uResolution;
    cellCenterUV = clamp(cellCenterUV, 0.0, 1.0);
    vec4 cellSample = IMG_NORM_PIXEL(inputImage, cellCenterUV);

    // Dot active_ if luminance exceeds threshold
    float active_ = step(uThreshold, subLuma);

    // Distance from subcell center
    vec2 d = subPos - 0.5;
    float dist = length(d);

    // Anti-aliased dot
    float pixelSize = 1.0 / (cellW * 0.5); // approximate pixel size in subcell space
    float dot = 1.0 - smoothstep(dotRadius - pixelSize, dotRadius + pixelSize, dist);
    dot *= active_;

    // Background color (dark)
    vec3 bgColor = vec3(0.02);
    // Dot color: blend between white and source color
    vec3 dotColor = mix(vec3(1.0), cellSample.rgb, colorMix);
    // Modulate dot brightness by luminance
    dotColor *= mix(0.5, 1.0, subLuma);

    vec3 finalColor = mix(bgColor, dotColor, dot);

    gl_FragColor = vec4(finalColor, 1.0);
}
