/*{
	"CREDIT": "Pixel Stereo",
	"ISFVSN": "2",
	"DESCRIPTION": "Adjusting the white point by multiplying the image by a color",
	"CATEGORIES": [
		"Color Adjustment"
	],
	"INPUTS": [
		{
			"NAME": "inputImage",
			"TYPE": "image"
		},
		{
			"NAME": "newWhite",
			"TYPE": "color",
			"DEFAULT": [
				1.0,
				1.0,
				1.0,
				1.0
			]
		}
	]
}*/



void main() {
	vec4 tmpColorA = IMG_THIS_PIXEL(inputImage);
	gl_FragColor = tmpColorA * newWhite;
}