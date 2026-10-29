/*{
  "ISFVSN": "2.0",
  "CATEGORIES": [
    "Color Adjustment",
    "Filter"
  ],
  "INPUTS": [
    {
      "NAME": "inputImage",
      "TYPE": "image"
    },
    {
      "NAME": "keyColor",
      "TYPE": "color",
      "DEFAULT": [0.0, 1.0, 0.0, 1.0],
      "LABEL": "Couleur à supprimer (Vert/Bleu)"
    },
    {
      "NAME": "similarity",
      "TYPE": "float",
      "MIN": 0.0,
      "MAX": 1.0,
      "DEFAULT": 0.4,
      "LABEL": "Tolérance / Similarité"
    },
    {
      "NAME": "smoothness",
      "TYPE": "float",
      "MIN": 0.0,
      "MAX": 1.0,
      "DEFAULT": 0.1,
      "LABEL": "Lissage des contours"
    },
    {
      "NAME": "spillReduction",
      "TYPE": "float",
      "MIN": 0.0,
      "MAX": 1.0,
      "DEFAULT": 0.2,
      "LABEL": "Correction des reflets (Spill)"
    }
  ]
}*/

// Chroma key in the CbCr plane, so lighter and darker shades of the key colour are removed alike.
vec2 chroma(vec3 rgb)
{
	return vec2(dot(rgb, vec3(-0.168736, -0.331264, 0.5)),
	            dot(rgb, vec3(0.5, -0.418688, -0.081312)));
}

void main() {
	vec4 color = IMG_THIS_PIXEL(inputImage);
	float dist = distance(chroma(color.rgb), chroma(keyColor.rgb)) - similarity;

	// Transparent inside the tolerance, ramping back to opaque over the smoothness width
	float mask = pow(clamp(dist / max(smoothness, 0.0001), 0.0, 1.0), 1.5);

	// Near the key, desaturate towards luminance to remove the coloured spill on the edges
	float spill = pow(clamp(dist / max(spillReduction, 0.0001), 0.0, 1.0), 1.5);
	float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
	vec3 rgb = mix(vec3(luma), color.rgb, spillReduction > 0.0 ? spill : 1.0);

	gl_FragColor = vec4(rgb, color.a * mask);
}
