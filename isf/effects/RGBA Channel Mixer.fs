/*
{
  "ISFVSN": "2",
  "TITLE": "RGBA Channel Mixer",
  "DESCRIPTION": "Allows free reassignment of each channel (R, G, B, A) from any source via 4 independent menus.",
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
      "NAME": "redSource",
      "LABEL": "Red Channel Source",
      "TYPE": "long",
      "VALUES": [0, 1, 2, 3, 4, 5],
      "LABELS": ["Red", "Green", "Blue", "Alpha", "Black (0.0)", "White (1.0)"],
      "DEFAULT": 0
    },
    {
      "NAME": "greenSource",
      "LABEL": "Green Channel Source",
      "TYPE": "long",
      "VALUES": [0, 1, 2, 3, 4, 5],
      "LABELS": ["Red", "Green", "Blue", "Alpha", "Black (0.0)", "White (1.0)"],
      "DEFAULT": 1
    },
    {
      "NAME": "blueSource",
      "LABEL": "Blue Channel Source",
      "TYPE": "long",
      "VALUES": [0, 1, 2, 3, 4, 5],
      "LABELS": ["Red", "Green", "Blue", "Alpha", "Black (0.0)", "White (1.0)"],
      "DEFAULT": 2
    },
    {
      "NAME": "alphaSource",
      "LABEL": "Alpha Channel Source",
      "TYPE": "long",
      "VALUES": [0, 1, 2, 3, 4, 5],
      "LABELS": ["Red", "Green", "Blue", "Alpha", "Black (0.0)", "White (1.0)"],
      "DEFAULT": 3
    }
  ]
}
*/

// Utility function to extract the value based on the menu choice
float getChannelValue(vec4 col, int sourceIndex) {
    if (sourceIndex == 0) return col.r;
    if (sourceIndex == 1) return col.g;
    if (sourceIndex == 2) return col.b;
    if (sourceIndex == 3) return col.a;
    if (sourceIndex == 4) return 0.0;
    if (sourceIndex == 5) return 1.0;
    return 0.0;
}

void main() {
    // Sampling the source image at the current pixel coordinates
    vec4 color = IMG_NORM_PIXEL(inputImage, isf_FragNormCoord);
    
    // Constructing the new color vector according to the 4 menus
    float rOut = getChannelValue(color, redSource);
    float gOut = getChannelValue(color, greenSource);
    float bOut = getChannelValue(color, blueSource);
    float aOut = getChannelValue(color, alphaSource);
    
    gl_FragColor = vec4(rOut, gOut, bOut, aOut);
}