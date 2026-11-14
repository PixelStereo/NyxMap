/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Woodcut print aesthetic with carved relief grooves. Adapted from Ghost Arcade's Linocut effect.",
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
            "LABEL": "Detail",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "amount2",
            "LABEL": "Groove Spacing",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "amount3",
            "LABEL": "Ink Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.7
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

// Ported from Ghost Arcade (linocutShader) by Pixel Stereo. Licensed under the AGPL-3.0.
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
    // Detail level: blur radius 0 to 4 pixels
    float detail = mix(0.0, 4.0, 1.0 - uAmount);
    // Groove spacing: 2 to 12 pixels
    float grooveSpacing = mix(2.0, 12.0, uAmount2);
    // Ink intensity: 0.3 to 1.0
    float inkIntensity = mix(0.3, 1.0, uAmount3);

    vec2 uv = vUv;
    vec2 texel = 1.0 / uResolution;
    vec2 fragCoord = uv * uResolution;

    // Pre-blur for detail control
    vec3 blurred = vec3(0.0);
    float blurTotal = 0.0;
    int blurRadius = int(detail);
    for (int i = -4; i <= 4; i++) {
        for (int j = -4; j <= 4; j++) {
            if (abs(i) > blurRadius || abs(j) > blurRadius) continue;
            float w = exp(-float(i*i + j*j) / max(detail * detail * 0.5, 0.1));
            blurred += IMG_NORM_PIXEL(inputImage, uv + vec2(float(i), float(j)) * texel).rgb * w;
            blurTotal += w;
        }
    }
    blurred /= blurTotal;
    float l = luma(blurred);

    // Edge detection for groove direction
    float gx = sampleLuma(uv + vec2(texel.x, 0.0)) - sampleLuma(uv - vec2(texel.x, 0.0));
    float gy = sampleLuma(uv + vec2(0.0, texel.y)) - sampleLuma(uv - vec2(0.0, texel.y));
    float edgeMag = length(vec2(gx, gy));
    float edgeAngle = atan(gy, gx);

    // Groove pattern: lines that follow contour direction
    // Perpendicular to gradient = along the contour
    float contourAngle = edgeAngle + 1.5708; // + PI/2
    vec2 contourDir = vec2(cos(contourAngle), sin(contourAngle));

    // Project fragment position onto contour perpendicular direction
    vec2 grooveDir = vec2(cos(edgeAngle), sin(edgeAngle));
    float groovePhase = dot(fragCoord, grooveDir) / grooveSpacing;
    float grooveLine = abs(fract(groovePhase) - 0.5) * 2.0;

    // In dark areas, more grooves (more ink carved away reveals paper)
    // Actually in linocut: dark = ink, light = carved away (paper)
    // Grooves create texture in mid-tones
    float grooveMask = smoothstep(0.3, 0.5, grooveLine);

    // High contrast conversion
    float paperThresh = uThreshold;
    float inkMask = smoothstep(paperThresh + 0.1, paperThresh - 0.1, l);

    // In mid-tone areas, use groove pattern
    float midMask = 1.0 - abs(l - 0.5) * 2.0;
    midMask = clamp(midMask, 0.0, 1.0);

    // Combine: dark areas solid ink, mid areas grooved, light areas paper
    float finalInk = inkMask;
    // Add groove texture in mid-tones
    finalInk = mix(finalInk, finalInk * grooveMask, midMask * 0.5);

    // Edge enhancement
    float edgeBoost = smoothstep(0.02, 0.15, edgeMag);
    finalInk = max(finalInk, edgeBoost * 0.7);

    // Paper color (warm off-white)
    vec3 paperColor = vec3(0.92, 0.89, 0.82);
    // Ink color (near black)
    vec3 inkColor = vec3(0.05, 0.04, 0.06) * inkIntensity;

    // Add subtle paper texture
    float paperNoise = hash(fragCoord * 0.5) * 0.04;
    paperColor += paperNoise;

    vec3 finalColor = mix(paperColor, inkColor, finalInk);

    gl_FragColor = vec4(finalColor, 1.0);
}
