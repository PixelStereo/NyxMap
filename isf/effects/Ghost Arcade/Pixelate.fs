/*{
    "ISFVSN": "2",
    "DESCRIPTION": "Mosaic / pixelation effect. Adapted from Ghost Arcade's Pixelate effect.",
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
            "NAME": "pixelateSize",
            "LABEL": "Size",
            "TYPE": "float",
            "MIN": 1.0,
            "MAX": 64.0,
            "DEFAULT": 8.0
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
            "NAME": "gridLines",
            "LABEL": "Grid Lines",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "animSpeed",
            "LABEL": "Anim Speed",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        },
        {
            "NAME": "animAmount",
            "LABEL": "Anim Amount",
            "TYPE": "float",
            "MIN": 0.0,
            "MAX": 1.0,
            "DEFAULT": 0.0
        }
    ]
}*/

// Ported from Ghost Arcade (pixelateShader) by Pixel Stereo. Licensed under the AGPL-3.0.
#define vUv isf_FragNormCoord
#define uTime TIME
#define uResolution RENDERSIZE
#define uSize (pixelateSize)
#define uMode float(floor(modeValue + 0.5))
#define uGridLines (gridLines)
#define uAnimSpeed (animSpeed)
#define uAnimAmount (animAmount)

        // 1-64 pixel size
        // 0=nearest, 1=luma mosaic, 2=hex, 3=circle/LED
   // 0-1 dark grout lines between pixels (LED panel look)
   // 0-2 size pulse speed (0 = static)
  // 0-1 size pulse amplitude

void main() {
  int mode = int(uMode + 0.5);

  // Animated size — sin oscillation around uSize.
  float sz = uSize;
  if (uAnimSpeed > 0.001) {
    float w = sin(uTime * uAnimSpeed * 3.14159) * 0.5 + 0.5;
    sz = uSize * mix(1.0 - uAnimAmount * 0.5, 1.0 + uAnimAmount * 1.0, w);
  }
  sz = max(sz, 1.0);

  vec2 pixelSize = sz / uResolution;
  vec2 cellId = floor(vUv / pixelSize);
  vec2 cellUv = (cellId + 0.5) * pixelSize;
  vec2 cellLocal = (vUv - cellId * pixelSize) / pixelSize; // 0..1 inside cell

  vec4 sample0 = IMG_NORM_PIXEL(inputImage, cellUv);

  if (mode == 1) {
    // Luma mosaic — each cell is rendered as a flat luma-step value
    // (3 steps per channel) so the image becomes a chunky comic look.
    vec3 q = floor(sample0.rgb * 4.0) / 3.0;
    gl_FragColor = vec4(q, sample0.a);
    return;
  }

  if (mode == 2) {
    // Hex — only pixels inside a hexagonal cell pass through;
    // approximate by clamping to a hex distance.
    vec2 d = cellLocal - 0.5;
    float hex = max(abs(d.x), max(abs(d.y), abs(d.x) * 0.5 + abs(d.y) * 0.866));
    if (hex > 0.5) {
      gl_FragColor = vec4(0.0, 0.0, 0.0, sample0.a * 0.4);
      return;
    }
  }

  if (mode == 3) {
    // Circle / LED — pixels are circles with brightness = sampled luma.
    vec2 d = cellLocal - 0.5;
    float dist = length(d);
    float disc = smoothstep(0.5, 0.45, dist);
    gl_FragColor = vec4(sample0.rgb * disc, sample0.a);
    return;
  }

  // Grid lines (LED grout). Darken the cell edge.
  if (uGridLines > 0.001) {
    vec2 edge = abs(cellLocal - 0.5);
    float onEdge = step(0.46, max(edge.x, edge.y));
    sample0.rgb *= mix(1.0, 0.0, onEdge * uGridLines);
  }

  gl_FragColor = sample0;
}
