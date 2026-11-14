/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Moving scan plane reveals or erases image with glow + sparks. Horizontal / vertical / diagonal / radial. Adapted from Ghost Arcade's Laser Slice effect.",
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
            "NAME": "modeValue",
            "LABEL": "Mode",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 8.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "speed",
            "LABEL": "Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "beamWidth",
            "LABEL": "Beam Width",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.02
        },
        {
            "NAME": "glow",
            "LABEL": "Glow",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.4,
            "DEFAULT": 1.2
        },
        {
            "NAME": "sparks",
            "LABEL": "Sparks",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.4
        },
        {
            "NAME": "eraseAmount",
            "LABEL": "Erase Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.5
        },
        {
            "NAME": "tintR",
            "LABEL": "Tint R",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "tintG",
            "LABEL": "Tint G",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
        },
        {
            "NAME": "tintB",
            "LABEL": "Tint B",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.1
        },
        {
            "NAME": "revealValue",
            "LABEL": "Reveal",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 2.0,
            "DEFAULT": 1.0
        },
        {
            "NAME": "persistence",
            "LABEL": "Persistence",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.84,
            "DEFAULT": 0.92
        }
    ],
    "PASSES": [
        {
            "TARGET": "feedbackBuffer",
            "PERSISTENT": true,
            "FLOAT": true
        },
        {}
    ]
}*/

// Ported from Ghost Arcade (laserSliceHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uMode float(floor(modeValue + 0.5))
#define uSpeed (speed)
#define uBeamWidth (beamWidth)
#define uGlow (glow)
#define uSparks (sparks)
#define uEraseAmount (eraseAmount)
#define uTintR (tintR)
#define uTintG (tintG)
#define uTintB (tintB)
#define uReveal (revealValue)
#define uPersistence (persistence)
#define uHasFeedback (FRAMEINDEX > 0 ? 1.0 : 0.0)

          // 0=horizontal, 1=vertical, 2=diagonal, 3=radial
         // 0-3
     // 0.005-0.1
          // 0-2
        // 0-1
   // 0-1 (use feedback for erased trail)



        // 0=erase pre-beam, 1=reveal post-beam
   // 0-1 trail decay

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void ghostArcadeEffect() {
  int mode = int(uMode + 0.5);
  float pos = mod(uTime * uSpeed * 0.3, 1.4) - 0.2; // beam pos 0..1 with sweep over edges

  // Distance from beam line
  float d;
  if (mode == 0) d = abs(vUv.y - pos);
  else if (mode == 1) d = abs(vUv.x - pos);
  else if (mode == 2) d = abs((vUv.x + vUv.y) * 0.5 - pos);
  else {
    // Radial: pos = ring radius
    vec2 rd = vUv - 0.5;
    d = abs(length(rd) - pos * 0.7);
  }

  float beam = smoothstep(uBeamWidth * 1.5, 0.0, d);
  float halo = exp(-d * d / (uBeamWidth * uBeamWidth * 8.0)) * uGlow;

  // Side relative to beam
  float side;
  if (mode == 0) side = step(vUv.y, pos);
  else if (mode == 1) side = step(vUv.x, pos);
  else if (mode == 2) side = step((vUv.x + vUv.y) * 0.5, pos);
  else side = step(length(vUv - 0.5), pos * 0.7);

  // Erase / reveal mask: 1 = show source, 0 = show feedback (trail)
  float mask = (uReveal > 0.5) ? side : (1.0 - side);

  vec3 src = IMG_NORM_PIXEL(inputImage, vUv).rgb;
  vec3 prev = (uHasFeedback > 0.5) ? IMG_NORM_PIXEL(feedbackBuffer, vUv).rgb : vec3(0.0);
  vec3 base = mix(prev * uPersistence, src, mask);

  // Erase amount controls how much we wipe through to feedback
  base = mix(src, base, uEraseAmount);

  vec3 tint = vec3(uTintR, uTintG, uTintB);
  vec3 result = base + tint * (beam + halo);

  // Sparks at beam edge
  if (uSparks > 0.001 && beam > 0.001) {
    float sp = step(0.97, hash21(floor(vUv * 300.0) + floor(uTime * 30.0)));
    result += sp * tint * 2.0 * uSparks;
  }

  gl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}

void main() {
    if (PASSINDEX == 0) ghostArcadeEffect();
    else gl_FragColor = IMG_NORM_PIXEL(feedbackBuffer, isf_FragNormCoord);
}
