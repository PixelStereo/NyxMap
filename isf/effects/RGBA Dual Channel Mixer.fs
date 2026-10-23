/*
{
  "ISFVSN": "2",
  "TITLE": "RGBA Channel Mixer Dual Source",
  "DESCRIPTION": "Allows free reassignment of each output channel (R, G, B, A) by picking from the channels of the main image or a second source.",
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
      "NAME": "secondImage",
      "TYPE": "image"
    },
    {
      "NAME": "redSource",
      "LABEL": "Red Channel Source",
      "TYPE": "long",
      "VALUES": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
      "LABELS": [
        "Image 1 - Red", "Image 1 - Green", "Image 1 - Blue", "Image 1 - Alpha",
        "Image 2 - Red", "Image 2 - Green", "Image 2 - Blue", "Image 2 - Alpha",
        "Black (0.0)", "White (1.0)"
      ],
      "DEFAULT": 0
    },
    {
      "NAME": "greenSource",
      "LABEL": "Green Channel Source",
      "TYPE": "long",
      "VALUES": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
      "LABELS": [
        "Image 1 - Red", "Image 1 - Green", "Image 1 - Blue", "Image 1 - Alpha",
        "Image 2 - Red", "Image 2 - Green", "Image 2 - Blue", "Image 2 - Alpha",
        "Black (0.0)", "White (1.0)"
      ],
      "DEFAULT": 1
    },
    {
      "NAME": "blueSource",
      "LABEL": "Blue Channel Source",
      "TYPE": "long",
      "VALUES": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
      "LABELS": [
        "Image 1 - Red", "Image 1 - Green", "Image 1 - Blue", "Image 1 - Alpha",
        "Image 2 - Red", "Image 2 - Green", "Image 2 - Blue", "Image 2 - Alpha",
        "Black (0.0)", "White (1.0)"
      ],
      "DEFAULT": 2
    },
    {
      "NAME": "alphaSource",
      "LABEL": "Alpha Channel Source",
      "TYPE": "long",
      "VALUES": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
      "LABELS": [
        "Image 1 - Red", "Image 1 - Green", "Image 1 - Blue", "Image 1 - Alpha",
        "Image 2 - Red", "Image 2 - Green", "Image 2 - Blue", "Image 2 - Alpha",
        "Black (0.0)", "White (1.0)"
      ],
      "DEFAULT": 3
    }
  ]
}
*/

// Utility function to extract the value from the correct source based on the menu index
float getChannelValue(vec4 col1, vec4 col2, int sourceIndex) {
    if (sourceIndex == 0) return col1.r; // Image 1 - Red
    if (sourceIndex == 1) return col1.g; // Image 1 - Green
    if (sourceIndex == 2) return col1.b; // Image 1 - Blue
    if (sourceIndex == 3) return col1.a; // Image 1 - Alpha
    if (sourceIndex == 4) return col2.r; // Image 2 - Red
    if (sourceIndex == 5) return col2.g; // Image 2 - Green
    if (sourceIndex == 6) return col2.b; // Image 2 - Blue
    if (sourceIndex == 7) return col2.a; // Image 2 - Alpha
    if (sourceIndex == 8) return 0.0;    // Black
    if (sourceIndex == 9) return 1.0;    // White
    return 0.0;
}

void main() {
    // Sampling both input images
    vec4 col1 = IMG_NORM_PIXEL(inputImage, isf_FragNormCoord);
    vec4 col2 = IMG_NORM_PIXEL(secondImage, isf_FragNormCoord);
    
    // Constructing the final color based on each menu choice
    float rOut = getChannelValue(col1, col2, redSource);
    float gOut = getChannelValue(col1, col2, greenSource);
    float bOut = getChannelValue(col1, col2, blueSource);
    float aOut = getChannelValue(col1, col2, alphaSource);
    
    gl_FragColor = vec4(rOut, gOut, bOut, aOut);
}