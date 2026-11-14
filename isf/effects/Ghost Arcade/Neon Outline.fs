/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Glowing neon edge detection with bloom glow. Adapted from Ghost Arcade's Neon Outline effect.",
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
            "LABEL": "Edge Sensitivity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Glow Spread",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount3",
            "LABEL": "Glow Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.6
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

// Ported from Ghost Arcade (neonOutlineShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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

float sampleLuma(vec2 uv) {
    return luma(IMG_NORM_PIXEL(inputImage, clamp(uv, 0.0, 1.0)).rgb);
}

void main() {
    // Edge sensitivity: 0.5 to 5.0
    float sensitivity = mix(0.5, 5.0, uAmount);
    // Glow spread: 1 to 8 pixels
    float glowSpread = mix(1.0, 8.0, uAmount2);
    // Glow intensity: 0.5 to 3.0
    float glowIntensity = mix(0.5, 3.0, uAmount3);

    vec2 uv = vUv;
    vec2 texel = 1.0 / uResolution;

    // Sobel edge detection
    float tl = sampleLuma(uv + vec2(-texel.x, -texel.y));
    float tc = sampleLuma(uv + vec2(0.0,     -texel.y));
    float tr = sampleLuma(uv + vec2( texel.x, -texel.y));
    float ml = sampleLuma(uv + vec2(-texel.x,  0.0));
    float mr = sampleLuma(uv + vec2( texel.x,  0.0));
    float bl = sampleLuma(uv + vec2(-texel.x,  texel.y));
    float bc = sampleLuma(uv + vec2(0.0,      texel.y));
    float br = sampleLuma(uv + vec2( texel.x,  texel.y));

    float sobelX = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
    float sobelY = -tl - 2.0*tc - tr + bl + 2.0*bc + br;
    float edge = length(vec2(sobelX, sobelY)) * sensitivity;
    edge = clamp(edge, 0.0, 1.0);

    // Multi-sample_ glow: blur the edge for bloom effect
    float glow = 0.0;
    float totalWeight = 0.0;
    for (int i = -4; i <= 4; i++) {
        for (int j = -4; j <= 4; j++) {
            vec2 offset = vec2(float(i), float(j)) * texel * glowSpread;
            vec2 sampleUV = uv + offset;

            // Compute edge at this offset
            float stl = sampleLuma(sampleUV + vec2(-texel.x, -texel.y));
            float stc = sampleLuma(sampleUV + vec2(0.0,     -texel.y));
            float str2 = sampleLuma(sampleUV + vec2( texel.x, -texel.y));
            float sml = sampleLuma(sampleUV + vec2(-texel.x,  0.0));
            float smr = sampleLuma(sampleUV + vec2( texel.x,  0.0));
            float sbl = sampleLuma(sampleUV + vec2(-texel.x,  texel.y));
            float sbc = sampleLuma(sampleUV + vec2(0.0,      texel.y));
            float sbr = sampleLuma(sampleUV + vec2( texel.x,  texel.y));

            float sx = -stl - 2.0*sml - sbl + str2 + 2.0*smr + sbr;
            float sy = -stl - 2.0*stc - str2 + sbl + 2.0*sbc + sbr;
            float se = length(vec2(sx, sy)) * sensitivity;
            se = clamp(se, 0.0, 1.0);

            float w = exp(-float(i*i + j*j) / (glowSpread * glowSpread * 0.5));
            glow += se * w;
            totalWeight += w;
        }
    }
    glow /= totalWeight;

    // Neon color: use uColor as base tint
    vec3 neonBase = length(uColor) > 0.01 ? uColor : vec3(0.2, 0.6, 1.0);
    // Slight hue shift for outer glow
    vec3 neonOuter = neonBase * vec3(1.2, 0.8, 1.0);

    // Dark background
    vec3 bgColor = vec3(0.01);

    // Compose: sharp edge + soft glow
    vec3 finalColor = bgColor;
    finalColor += neonOuter * glow * glowIntensity * 0.5;
    finalColor += neonBase * edge * glowIntensity;

    // White core on strong edges
    float core = smoothstep(0.6, 0.9, edge);
    finalColor += vec3(1.0) * core * 0.5;

    gl_FragColor = vec4(finalColor, 1.0);
}
