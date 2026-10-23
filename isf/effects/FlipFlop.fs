/*{
    "DESCRIPTION": "Horizontal / vertical flip",
    "CREDIT": "Pixel Stereo",
    "ISFVSN": "2",
    "CATEGORIES": ["Geometry"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "flip", "LABEL": "Flip", "TYPE": "bool", "DEFAULT": false },
        { "NAME": "flop", "LABEL": "Flop", "TYPE": "bool", "DEFAULT": false }
    ]
}*/

void main()
{
    vec2 uv = isf_FragNormCoord;
    if (flip) uv.x = 1.0 - uv.x;
    if (flop) uv.y = 1.0 - uv.y;
    gl_FragColor = IMG_NORM_PIXEL(inputImage, uv);
}
