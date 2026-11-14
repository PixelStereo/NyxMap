/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Topographic contour map lines at luminance elevation intervals. Adapted from Ghost Arcade's Topo Map effect.",
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
            "LABEL": "Contour Density",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Line Thickness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount3",
            "LABEL": "Color Fill",
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
            "NAME": "angle",
            "LABEL": "Angle",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (topoMapShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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
    // Contour density: 4 to 40 contour levels
    float contourLevels = mix(4.0, 40.0, uAmount);
    // Line thickness: 0.5 to 3.0 pixels
    float lineThick = mix(0.5, 3.0, uAmount2);
    // Color fill amount: 0 = monochrome, 1 = full source color
    float colorFill = uAmount3;

    vec2 uv = vUv;
    vec2 texel = 1.0 / uResolution;

    // Sample and slightly blur for smoother contours
    vec3 blurred = vec3(0.0);
    float total = 0.0;
    for (int i = -2; i <= 2; i++) {
        for (int j = -2; j <= 2; j++) {
            float w = exp(-float(i*i + j*j) * 0.5);
            blurred += IMG_NORM_PIXEL(inputImage, uv + vec2(float(i), float(j)) * texel).rgb * w;
            total += w;
        }
    }
    blurred /= total;

    float elevation = luma(blurred);
    vec4 src = IMG_NORM_PIXEL(inputImage, uv);

    // Quantize elevation to contour levels
    float quantized = floor(elevation * contourLevels) / contourLevels;
    float nextLevel = (floor(elevation * contourLevels) + 1.0) / contourLevels;

    // Gradient magnitude (slope) for adaptive line thickness
    float ex = luma(IMG_NORM_PIXEL(inputImage, uv + vec2(texel.x, 0.0)).rgb);
    float wx = luma(IMG_NORM_PIXEL(inputImage, uv - vec2(texel.x, 0.0)).rgb);
    float ey = luma(IMG_NORM_PIXEL(inputImage, uv + vec2(0.0, texel.y)).rgb);
    float wy = luma(IMG_NORM_PIXEL(inputImage, uv - vec2(0.0, texel.y)).rgb);
    float slope = length(vec2(ex - wx, ey - wy)) * 0.5 / texel.x;

    // Contour line: where elevation crosses a contour level
    float levelFract = fract(elevation * contourLevels);
    // Distance to nearest contour boundary (0 or 1 in fract space)
    float distToContour = min(levelFract, 1.0 - levelFract);

    // Convert to pixel-space distance (approximate)
    float slopePixels = max(slope, 0.001); // avoid division by zero
    float pixelDist = distToContour / (slopePixels * texel.x) * (1.0 / contourLevels);

    // Line mask with anti-aliasing; thinner on steep slopes
    float adaptiveThick = lineThick / max(slopePixels * 0.01, 0.5);
    adaptiveThick = clamp(adaptiveThick, 0.5, lineThick * 2.0);
    float lineMask = 1.0 - smoothstep(adaptiveThick * 0.5 - 0.5, adaptiveThick * 0.5 + 0.5, pixelDist);

    // Major contour lines (every 5th level) are thicker
    float majorLevel = floor(elevation * contourLevels / 5.0) * 5.0 / contourLevels;
    float majorFract = fract(elevation * contourLevels / 5.0);
    float majorDist = min(majorFract, 1.0 - majorFract);
    float majorPixelDist = majorDist / (slopePixels * texel.x) * (5.0 / contourLevels);
    float majorMask = 1.0 - smoothstep(adaptiveThick - 0.5, adaptiveThick + 0.5, majorPixelDist);
    lineMask = max(lineMask, majorMask);

    // Background: subtle elevation coloring
    // Topo map style: greens at low, yellows mid, browns high
    vec3 topoLow = vec3(0.75, 0.88, 0.70);  // green
    vec3 topoMid = vec3(0.92, 0.88, 0.65);  // yellow
    vec3 topoHigh = vec3(0.78, 0.65, 0.50); // brown

    vec3 topoColor;
    if (elevation < 0.5) {
        topoColor = mix(topoLow, topoMid, elevation * 2.0);
    } else {
        topoColor = mix(topoMid, topoHigh, (elevation - 0.5) * 2.0);
    }

    // Mix topo coloring with source color based on colorFill
    vec3 fillColor = mix(topoColor, src.rgb * 0.8 + topoColor * 0.2, colorFill);

    // Line color: use uColor if provided, else dark brown
    vec3 lineColor = length(uColor) > 0.01 ? uColor : vec3(0.25, 0.18, 0.12);

    vec3 finalColor = mix(fillColor, lineColor, lineMask);

    gl_FragColor = vec4(finalColor, 1.0);
}
