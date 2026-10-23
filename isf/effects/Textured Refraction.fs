/*{
  "DESCRIPTION": "Refraction of a video stream through a textured translucent material: reeded/fluted glass, hammered glass, Fresnel lens, diamond point",
  "CREDIT": "Pixel Stereo",
  "ISFVSN": "2",
  "CATEGORIES": ["Distortion Effect", "Stylize"],
  "INPUTS": [
    { "NAME": "inputImage", "TYPE": "image" },
    { "NAME": "mode", "LABEL": "Material", "TYPE": "long",
      "VALUES": [0, 1, 2, 3],
      "LABELS": ["Reeded Glass", "Hammered Glass", "Fresnel Lens", "Diamond Point"],
      "DEFAULT": 0 },
    { "NAME": "density",    "LABEL": "Density (ribs/cells)", "TYPE": "float", "DEFAULT": 24.0, "MIN": 2.0,  "MAX": 200.0 },
    { "NAME": "angle",      "LABEL": "Angle",                    "TYPE": "float", "DEFAULT": 0.0,  "MIN": 0.0,  "MAX": 180.0 },
    { "NAME": "strength",   "LABEL": "Refraction",               "TYPE": "float", "DEFAULT": 0.8,  "MIN": -3.0, "MAX": 3.0 },
    { "NAME": "dispersion", "LABEL": "Chromatic Dispersion",   "TYPE": "float", "DEFAULT": 0.15, "MIN": 0.0,  "MAX": 1.0 },
    { "NAME": "frost",      "LABEL": "Frosted",                  "TYPE": "float", "DEFAULT": 0.1,  "MIN": 0.0,  "MAX": 1.0 },
    { "NAME": "sheen",      "LABEL": "Highlights / Edges",       "TYPE": "float", "DEFAULT": 0.3,  "MIN": 0.0,  "MAX": 1.0 },
    { "NAME": "tint",       "LABEL": "Material Tint",       "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0] },
    { "NAME": "speed",      "LABEL": "Scroll Speed",             "TYPE": "float", "DEFAULT": 0.0,  "MIN": -2.0, "MAX": 2.0 },
    { "NAME": "center",     "LABEL": "Center (Fresnel)",         "TYPE": "point2D", "DEFAULT": [0.5, 0.5], "MIN": [0.0, 0.0], "MAX": [1.0, 1.0] }
  ]
}*/

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * vnoise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}

void main() {
    vec2 uv = isf_FragNormCoord;
    float aspect = RENDERSIZE.x / RENDERSIZE.y;
    vec2 asp = vec2(aspect, 1.0);
    vec2 p = (uv - 0.5) * asp;                 // image space, corrected aspect ratio

    float a = radians(angle);
    float ca = cos(a);
    float sa = sin(a);
    mat2 rot    = mat2(ca, -sa, sa, ca);       // screen -> material
    mat2 invRot = mat2(ca, sa, -sa, ca);       // material -> screen
    vec2 q = rot * p;

    float t = TIME * speed;
    float cell = 1.0 / density;
    vec2 n = vec2(0.0);                        // surface slope (material space)
    float edge = 0.0;

    if (mode == 0) {
        // Reeded: row of semi-cylinders -> each rib shows an inverted/compressed copy
        float x = fract(q.x * density + t) - 0.5;
        float h = sqrt(max(1.0 - 4.0 * x * x, 0.02));
        n = vec2(2.0 * x / h, 0.0);
        edge = 1.0 - h;
    } else if (mode == 1) {
        // Hammered: organic relief (fractal noise)
        vec