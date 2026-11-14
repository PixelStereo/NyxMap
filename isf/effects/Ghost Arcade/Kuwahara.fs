/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Painterly smoothing that preserves edges. Adapted from Ghost Arcade's Kuwahara effect.",
    "CREDIT": "Ghost Arcade (riskcapital), adapted to ISF by Pixel Stereo",
    "LICENSE": "AGPL-3.0 (derived from Ghost Arcade, https://github.com/riskcapital/ghost-arcade)",
    "CATEGORIES": [
        "Stylize",
        "Ghost Arcade"
    ],
    "INPUTS": [
        {
            "NAME": "inputImage",
            "TYPE": "image"
        },
        {
            "NAME": "radiusValue",
            "LABEL": "Radius",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 6.0,
            "DEFAULT": 3.0
        },
        {
            "NAME": "edgeSharpness",
            "LABEL": "Edge Sharpness",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.3
        },
        {
            "NAME": "colorPunch",
            "LABEL": "Color Punch",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.2
        }
    ]
}*/

// Ported from Ghost Arcade (kuwaharaHeroShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uRadius (radiusValue)
#define uEdgeSharpness (edgeSharpness)
#define uColorPunch (colorPunch)

        // 1-8 pixel radius of each quadrant
 // 0-1 extra contrast on the chosen quadrant
    // 0-1 saturation boost on output_

void main() {
  vec2 t = 1.0 / uResolution;
  float r = max(uRadius, 1.0);

  // 4-quadrant Kuwahara (cheap variant; 9-quadrant is too costly per
  // pixel for live preview). Sample mean + variance in each of TL/TR/BL/BR
  // quadrants, then pick the one with the lowest variance.
  vec3 means[4];
  float vars[4];
  for (int q = 0; q < 4; q++) {
    vec2 dir = vec2((q == 0 || q == 2) ? -1.0 : 1.0,
                     (q < 2) ? 1.0 : -1.0);
    vec3 sum = vec3(0.0);
    vec3 sumSq = vec3(0.0);
    float n = 0.0;
    // Sample a 3x3 stencil scaled by radius in the quadrant direction.
    for (int i = 0; i <= 2; i++) {
      for (int j = 0; j <= 2; j++) {
        vec2 off = (vec2(float(i), float(j))) * dir * r * t;
        vec3 c = IMG_NORM_PIXEL(inputImage, vUv + off).rgb;
        sum += c;
        sumSq += c * c;
        n += 1.0;
      }
    }
    vec3 m = sum / n;
    vec3 v = sumSq / n - m * m;
    means[q] = m;
    vars[q] = v.r + v.g + v.b;
  }

  // Pick lowest-variance quadrant.
  int bestQ = 0;
  float bestV = vars[0];
  if (vars[1] < bestV) { bestV = vars[1]; bestQ = 1; }
  if (vars[2] < bestV) { bestV = vars[2]; bestQ = 2; }
  if (vars[3] < bestV) { bestV = vars[3]; bestQ = 3; }
  vec3 result = means[bestQ];

  // Edge sharpness — push toward the chosen mean by overshooting.
  if (uEdgeSharpness > 0.001) {
    vec3 center = IMG_NORM_PIXEL(inputImage, vUv).rgb;
    result = mix(result, result + (result - center) * 0.5, uEdgeSharpness);
  }

  // Color punch — saturation boost on output_.
  if (uColorPunch > 0.001) {
    float lum = dot(result, vec3(0.299, 0.587, 0.114));
    result = mix(vec3(lum), result, 1.0 + uColorPunch);
  }

  vec4 texColor = IMG_NORM_PIXEL(inputImage, vUv);
  gl_FragColor = vec4(clamp(result, 0.0, 1.0), texColor.a);
}
