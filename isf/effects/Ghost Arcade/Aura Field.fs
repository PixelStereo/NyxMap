/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Soft color fields bloom from edges + audio peaks with hue cycling and add/screen/replace blends. Adapted from Ghost Arcade's Aura Field effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "New Hero",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "intensity",
            "LABEL": "Intensity",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "radius",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 24.0,
            "DEFAULT": 12.0
        },
        {
            "NAME": "edgeAmount",
            "LABEL": "Edge Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "lumaAmount",
            "LABEL": "Luma Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "hueShift",
            "LABEL": "Hue Shift",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.2,
            "DEFAULT": 0.6
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.7,
            "DEFAULT": 0.85
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "useVisionMask",
            "LABEL": "Use Vision mask",
            "TYPE": "bool",
            "DEFAULT": false
        },
        {
            "NAME": "visionMask",
            "LABEL": "Vision mask (a MediaPipe Foreground Mask layer)",
            "TYPE": "image"
        }
    ]
}*/

// Ported from Ghost Arcade (auraFieldHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uIntensity (intensity)
#define uRadius (radius)
#define uEdgeAmount (edgeAmount)
#define uLumaAmount (lumaAmount)
#define uAudio 0.0
#define uAudioReact 0.5
#define uHueShift (hueShift)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uMode float(floor(modeValue + 0.5))
#define uHasNativeMask (useVisionMask ? 1.0 : 0.0)

vec4 ghostVisionMask(vec2 uv) {
    vec4 m = IMG_NORM_PIXEL(visionMask, uv);
    return vec4(min(m.r, m.a));
}


  
       // 0-2
          // 4-32 px
      // 0-1 edge contribution
      // 0-1 bright pixel contribution
  
      // 0-2 audio scales aura
        // 0-1
  
  
  
            // 0=add, 1=screen, 2=replace

  float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
  vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
  }

  void main() {
    vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
    float maskCenter = ghostVisionMask( vUv).r;
    float hasMask = step(0.5, uHasNativeMask);
    float maskSoft = smoothstep(0.08, 0.85, maskCenter);
    vec2 texel = 1.0 / uResolution;

    // Compute aura from neighborhood: blur weighted by source brightness + edges
    vec3 aura = vec3(0.0);
    float wsum = 0.0;
    float r = uRadius * (1.0 + uAudio * uAudioReact * 0.6);
    for (int y = -4; y <= 4; y++) {
      for (int x = -4; x <= 4; x++) {
        if (abs(x) + abs(y) > 6) continue;
        vec2 off = vec2(float(x), float(y)) * texel * r * 0.4;
        vec3 s = IMG_NORM_PIXEL(inputImage, vUv + off).rgb;
        float m = ghostVisionMask( vUv + off).r;
        // Edge component (delta from center)
        vec3 d = abs(s - src);
        float rgbEdge = d.r + d.g + d.b;
        float maskEdge = abs(m - maskCenter) * 3.0;
        float edgeW = mix(rgbEdge, max(rgbEdge * 0.35, maskEdge), hasMask) * uEdgeAmount;
        float lumaW = mix(luma(s), max(luma(s) * 0.35, m), hasMask) * uLumaAmount;
        float w = exp(-(float(x*x + y*y)) / 8.0) * (edgeW + lumaW);
        vec3 auraSource = mix(s, mix(s, vec3(1.0), 0.55), hasMask * m);
        aura += auraSource * w;
        wsum += w;
      }
    }
    aura = (wsum > 0.0001) ? aura / wsum : vec3(0.0);
    aura *= mix(1.0, 0.45 + maskSoft * 0.75 + smoothstep(0.02, 0.32, abs(aura.r + aura.g + aura.b)) * 0.25, hasMask);
    aura *= uIntensity * (1.0 + uAudio * uAudioReact);

    // Optional hue cycle
    if (uHueShift > 0.001) {
      float l = luma(aura);
      vec3 hsvAura = hsv2rgb(vec3(fract(uTime * 0.1 + uHueShift), 1.0, l));
      aura = mix(aura, hsvAura, uHueShift);
    }
    aura *= vec3(uTintR, uTintG, uTintB);

    int mode = int(uMode + 0.5);
    vec3 result;
    if (mode == 0) result = src + aura;
    else if (mode == 1) result = 1.0 - (1.0 - src) * (1.0 - aura);
    else result = mix(src, aura, clamp(uIntensity * 0.5, 0.0, 1.0));

    gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
  }
