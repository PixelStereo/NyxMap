/*{
    "ISFVSN": "2",
    "DESCRIPTION": "LED display wall with visible RGB sub-pixel elements and bloom. Adapted from Ghost Arcade's LED Wall effect.",
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
            "LABEL": "LED Size",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount2",
            "LABEL": "Roundness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount3",
            "LABEL": "Bloom",
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

// Ported from Ghost Arcade (ledWallShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

// Rounded rectangle SDF
float roundedRect(vec2 p, vec2 halfSize, float radius) {
    vec2 d = abs(p) - halfSize + radius;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

void main() {
    // LED size: 4 to 40 pixels per LED
    float ledPixels = mix(4.0, 40.0, uAmount);
    // Roundness: 0.0 (square) to 0.45 (very round)
    float roundness = mix(0.0, 0.45, uAmount2);
    // Bloom amount: 0 to 1
    float bloomAmt = uAmount3;

    vec2 fragCoord = vUv * uResolution;

    // LED grid
    vec2 ledIndex = floor(fragCoord / ledPixels);
    vec2 ledPos = fract(fragCoord / ledPixels); // 0..1 within LED cell

    // Sample source at LED center
    vec2 ledCenterUV = (ledIndex + 0.5) * ledPixels / uResolution;
    ledCenterUV = clamp(ledCenterUV, 0.0, 1.0);
    vec4 src = IMG_NORM_PIXEL(inputImage, ledCenterUV);

    // LED outer shape (with gap)
    float gap = 0.08; // gap between LEDs
    vec2 ledHalfSize = vec2(0.5 - gap);
    float ledDist = roundedRect(ledPos - 0.5, ledHalfSize, roundness);
    float ledMask = 1.0 - smoothstep(-0.01, 0.01, ledDist);

    // RGB sub-pixel layout (3 vertical stripes within the LED)
    float subPixelWidth = (1.0 - gap * 2.0) / 3.0;
    float subGap = 0.01;

    // Position within the LED active_ area
    float localX = ledPos.x - gap;
    float ledWidth = 1.0 - gap * 2.0;

    float rStripe = 0.0;
    float gStripe = 0.0;
    float bStripe = 0.0;

    if (localX > 0.0 && localX < ledWidth) {
        float normalized = localX / ledWidth;
        // R sub-pixel: 0 to 0.333
        rStripe = smoothstep(0.0, subGap, normalized) * (1.0 - smoothstep(0.333 - subGap, 0.333, normalized));
        // G sub-pixel: 0.333 to 0.666
        gStripe = smoothstep(0.333, 0.333 + subGap, normalized) * (1.0 - smoothstep(0.666 - subGap, 0.666, normalized));
        // B sub-pixel: 0.666 to 1.0
        bStripe = smoothstep(0.666, 0.666 + subGap, normalized) * (1.0 - smoothstep(1.0 - subGap, 1.0, normalized));
    }

    // Each sub-pixel shows its respective channel boosted
    vec3 subPixelColor;
    subPixelColor.r = src.r * rStripe * 1.3;
    subPixelColor.g = src.g * gStripe * 1.3;
    subPixelColor.b = src.b * bStripe * 1.3;

    // Also add some base mixed color to avoid pure black between sub-pixels
    vec3 baseLedColor = src.rgb * 0.3;

    vec3 ledColor = (subPixelColor + baseLedColor) * ledMask;

    // Bloom/glow: soft glow around bright LEDs
    float brightness = luma(src.rgb);
    float glowDist = length(ledPos - 0.5);
    float glow = exp(-glowDist * glowDist * 8.0) * brightness * bloomAmt * 0.5;
    vec3 glowColor = src.rgb * glow;

    // Background (very dark, between LEDs)
    vec3 bgColor = vec3(0.01);

    vec3 finalColor = bgColor + ledColor + glowColor;

    // Clamp to prevent overblown highlights
    finalColor = min(finalColor, vec3(1.2));

    gl_FragColor = vec4(finalColor, 1.0);
}
