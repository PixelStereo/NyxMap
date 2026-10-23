/*
{
  "ISFVSN": "2",
  "TITLE": "RGBA Channel Inspector",
  "DESCRIPTION": "Displays the image either in a 4-quadrant split mode (R, G, B, A), or channel by channel in full screen.",
  "CREDIT": "Pixel Stereo",
  "CATEGORIES": [
    "Color Adjustment",
    "Utility"
  ],
  "INPUTS": [
    {
      "NAME": "inputImage",
      "TYPE": "image"
    },
    {
      "NAME": "displayMode",
      "LABEL": "Display Mode",
      "TYPE": "long",
      "VALUES": [0, 1, 2, 3, 4],
      "LABELS": [
        "Split 4 (R, G, B, A)",
        "Red (Full Screen)",
        "Green (Full Screen)",
        "Blue (Full Screen)",
        "Alpha (Full Screen)"
      ],
      "DEFAULT": 0
    },
    {
      "NAME": "tintChannels",
      "LABEL": "Tint Channels",
      "TYPE": "bool",
      "DEFAULT": false
    }
  ]
}
*/

void main() {
    vec4 color = IMG_NORM_PIXEL(inputImage, isf_FragNormCoord);
    
    // Mode 0: 2x2 Split (R, G, B, A)
    if (displayMode == 0) {
        vec2 uv = isf_FragNormCoord;
        vec2 grid = uv * 2.0;
        vec2 fGrid = fract(grid);
        vec2 iGrid = floor(grid);
        
        vec4 splitColor = IMG_NORM_PIXEL(inputImage, fGrid);
        float val = 0.0;
        vec3 tintColor = vec3(1.0);
        
        if (iGrid.y > 0.5) {
            if (iGrid.x < 0.5) {
                val = splitColor.r;
                tintColor = vec3(1.0, 0.0, 0.0);
            } else {
                val = splitColor.g;
                tintColor = vec3(0.0, 1.0, 0.0);
            }
        } else {
            if (iGrid.x < 0.5) {
                val = splitColor.b;
                tintColor = vec3(0.0, 0.0, 1.0);
            } else {
                val = splitColor.a;
                tintColor = vec3(1.0, 1.0, 1.0);
            }
        }
        
        vec3 finalColor = tintChannels ? (vec3(val) * tintColor) : vec3(val);
        gl_FragColor = vec4(finalColor, 1.0);
    } 
    // Mode 1: Red Only
    else if (displayMode == 1) {
        float val = color.r;
        vec3 finalColor = tintChannels ? vec3(val, 0.0, 0.0) : vec3(val);
        gl_FragColor = vec4(finalColor, 1.0);
    }
    // Mode 2: Green Only
    else if (displayMode == 2) {
        float val = color.g;
        vec3 finalColor = tintChannels ? vec3(0.0, val, 0.0) : vec3(val);
        gl_FragColor = vec4(finalColor, 1.0);
    }
    // Mode 3: Blue Only
    else if (displayMode == 3) {
        float val = color.b;
        vec3 finalColor = tintChannels ? vec3(0.0, 0.0, val) : vec3(val);
        gl_FragColor = vec4(finalColor, 1.0);
    }
    // Mode 4: Alpha Only
    else if (displayMode == 4) {
        float val = color.a;
        gl_FragColor = vec4(vec3(val), 1.0);
    }
}