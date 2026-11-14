/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Luminance-based pixel sorting with vertical streak artifacts. Adapted from Ghost Arcade's Pixel Sort effect.",
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
            "LABEL": "Sort Distance",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Threshold",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount3",
            "LABEL": "Chromatic Sep",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
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

// Ported from Ghost Arcade (pixelSortShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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
    // Sort distance: 0 to 60 pixels
    float sortDist = mix(0.0, 60.0, uAmount);
    // Luminance threshold for which pixels get sorted
    float sortThresh = mix(0.0, 1.0, uAmount2);
    // Chromatic separation: 0 to 5 pixels
    float chromSep = mix(0.0, 5.0, uAmount3);

    vec2 uv = vUv;
    vec2 texel = 1.0 / uResolution;

    // Sample current pixel
    vec4 src = IMG_NORM_PIXEL(inputImage, uv);
    float srcLuma = luma(src.rgb);

    // Determine if this pixel should participate in sorting
    float sortActive = step(sortThresh, srcLuma);

    // Vertical pixel sorting simulation:
    // Look upward for brighter pixels that would "sort" down to this position
    vec3 sortedR = src.rgb;
    vec3 sortedG = src.rgb;
    vec3 sortedB = src.rgb;

    float maxSteps = min(sortDist, 60.0);
    int steps = int(maxSteps);

    // Find the brightest pixel above that exceeds threshold
    float bestLuma = srcLuma;
    vec3 bestColor = src.rgb;

    for (int i = 1; i <= 60; i++) {
        if (float(i) > maxSteps) break;

        float fi = float(i);
        vec2 upUV = uv - vec2(0.0, fi * texel.y);
        if (upUV.y < 0.0) break;

        vec4 upSample = IMG_NORM_PIXEL(inputImage, upUV);
        float upLuma = luma(upSample.rgb);

        // Only sort bright pixels
        if (upLuma > sortThresh && upLuma > bestLuma) {
            bestLuma = upLuma;
            bestColor = upSample.rgb;
        }
    }

    // Blend sorted result based on activity
    float sortStrength = sortActive * smoothstep(sortThresh, sortThresh + 0.1, srcLuma);
    vec3 sorted = mix(src.rgb, bestColor, sortStrength * 0.6);

    // Chromatic separation on sorted pixels
    float chromOffset = chromSep * texel.y * sortStrength;
    float rChannel = IMG_NORM_PIXEL(inputImage, uv - vec2(0.0, chromOffset)).r;
    float gChannel = sorted.g;
    float bChannel = IMG_NORM_PIXEL(inputImage, uv + vec2(0.0, chromOffset)).b;

    // Vertical streak effect: elongate bright areas
    float streak = 0.0;
    for (int i = 1; i <= 60; i++) {
        if (float(i) > maxSteps * 0.5) break;
        vec2 aboveUV = uv - vec2(0.0, float(i) * texel.y);
        if (aboveUV.y < 0.0) break;
        float aboveLuma = luma(IMG_NORM_PIXEL(inputImage, aboveUV).rgb);
        if (aboveLuma > sortThresh) {
            float falloff = 1.0 - float(i) / (maxSteps * 0.5);
            streak = max(streak, aboveLuma * falloff * 0.3);
        }
    }

    vec3 finalColor = vec3(rChannel, gChannel, bChannel);
    finalColor += vec3(streak) * sortStrength;

    // Mix with original based on how much sorting is active_
    finalColor = mix(src.rgb, finalColor, sortActive * 0.8 + 0.2);

    gl_FragColor = vec4(finalColor, 1.0);
}
