/*{
    "DESCRIPTION": "The viewports' test card as a generator: grid, diagonals and a circle (round only when the aspect is right), a frame on the outermost pixels, numbered colored corners (1 red top left, 2 green top right, 3 blue bottom right, 4 yellow bottom left), color bars, grey steps, the picture's size, and motion so that a frozen output shows.",
    "CREDIT": "Pixel Stereo",
    "ISFVSN": "2",
    "CATEGORIES": ["Utility"],
    "INPUTS": [
        { "NAME": "showSize", "LABEL": "Size", "TYPE": "bool", "DEFAULT": true },
        { "NAME": "motion", "LABEL": "Motion", "TYPE": "bool", "DEFAULT": true }
    ]
}*/

// Same geometry and colors as the viewport test card (src/engine/TestCard.cpp).
// Coordinates in pixels from the top left, like the C++ card.

const vec3 BACKGROUND = vec3(28.0, 28.0, 30.0) / 255.0;

// 5 x 7 pixel font: 0-9, then 10 = '×'. One int per row, top row first, bit 4 = left column.
const int FONT[77] = int[77](
    14, 17, 19, 21, 25, 17, 14,   // 0
     4, 12,  4,  4,  4,  4, 14,   // 1
    14, 17,  1,  2,  4,  8, 31,   // 2
    31,  2,  4,  2,  1, 17, 14,   // 3
     2,  6, 10, 18, 31,  2,  2,   // 4
    31, 16, 30,  1,  1, 17, 14,   // 5
     6,  8, 16, 30, 17, 17, 14,   // 6
    31,  1,  2,  4,  8,  8,  8,   // 7
    14, 17, 17, 14, 17, 17, 14,   // 8
    14, 17, 17, 15,  1,  2, 12,   // 9
     0, 17, 10,  4, 10, 17,  0    // ×
);

// 1 where glyph `c` is lit at `cell` (glyph pixels, from its top left)
float glyph(int c, vec2 cell)
{
    if (c < 0 || cell.x < 0.0 || cell.y < 0.0 || cell.x >= 5.0 || cell.y >= 7.0) return 0.0;
    int row = FONT[c * 7 + int(cell.y)];
    return float((row >> (4 - int(cell.x))) & 1);
}

int digitCount(int n)
{
    int d = 1;
    for (int i = 0; i < 6; ++i) if (n >= 10) { n /= 10; ++d; }
    return d;
}

int digitAt(int n, int count, int i) // i-th digit from the left
{
    for (int k = 0; k < count - 1 - i; ++k) n /= 10;
    return n - (n / 10) * 10;
}

// "W × H": the glyph of character i (-1: a space)
int sizeChar(int w, int h, int i)
{
    int dw = digitCount(w);
    if (i < dw) return digitAt(w, dw, i);
    if (i == dw + 1) return 10;
    if (i == dw || i == dw + 2) return -1;
    return digitAt(h, digitCount(h), i - dw - 3);
}

// Antialiased coverage of a line `width` pixels wide at distance `d`
float line(float d, float width)
{
    return clamp(width * 0.5 + 0.5 - d, 0.0, 1.0);
}

bool inRect(vec2 q, vec4 r) // x, y, width, height
{
    return q.x >= r.x && q.x < r.x + r.z && q.y >= r.y && q.y < r.y + r.w;
}

float roundedBox(vec2 q, vec2 center, vec2 extent, float radius)
{
    vec2 d = abs(q - center) - extent + radius;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

void main()
{
    vec2 size = RENDERSIZE;
    float W = size.x, H = size.y;
    float unit = min(W, H);
    vec2 p = isf_FragNormCoord * size; // from the bottom left
    vec2 q = vec2(p.x, H - p.y);       // from the top left
    vec2 center = size * 0.5;
    float radius = unit * 0.40;
    float corner = unit * 0.13;
    vec4 bars = vec4(W * 0.16, H * 0.66, W * 0.68, H * 0.08);
    vec4 steps = vec4(bars.x, bars.y + bars.w, bars.z, H * 0.05);
    vec4 band = vec4(bars.x, steps.y + steps.w + H * 0.015, bars.z, H * 0.035);

    vec3 c = BACKGROUND;

    // Grid of squares through the center, the center cross a little brighter, the diagonals
    float step = max(8.0, floor(H / 10.0 + 0.5));
    vec2 g = abs(mod(q - center + step * 0.5, step) - step * 0.5);
    c = mix(c, vec3(85.0, 85.0, 90.0) / 255.0, line(min(g.x, g.y), max(1.0, unit / 1080.0)));
    vec2 dc = abs(q - center);
    c = mix(c, vec3(150.0, 150.0, 155.0) / 255.0, line(min(dc.x, dc.y), max(1.0, unit / 720.0)));
    float diagonal = min(abs(H * q.x - W * q.y), abs(H * (W - q.x) - W * q.y)) / length(size);
    c = mix(c, vec3(110.0, 110.0, 115.0) / 255.0, line(diagonal, max(1.0, unit / 1080.0)));

    // The circle: round only if the picture's aspect is right
    c = mix(c, vec3(1.0), line(abs(length(q - center) - radius), max(2.0, unit / 360.0)));

    // Color bars, grey steps, and the band where the block moves
    if (inRect(q, bars)) {
        int i = int(clamp(floor((q.x - bars.x) / bars.z * 8.0), 0.0, 7.0));
        vec3 colors[8] = vec3[8](vec3(1.0), vec3(1.0, 1.0, 0.0), vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0),
                                 vec3(1.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0));
        c = colors[i];
    }
    if (inRect(q, steps)) {
        float i = clamp(floor((q.x - steps.x) / steps.z * 11.0), 0.0, 10.0);
        c = vec3(floor(255.0 * i / 10.0 + 0.5) / 255.0);
    }
    if (inRect(q, band)) c = vec3(55.0, 55.0, 60.0) / 255.0;

    // The four corners: a colored triangle each, numbered
    vec2 at[4] = vec2[4](vec2(0.0, 0.0), vec2(W, 0.0), vec2(W, H), vec2(0.0, H));
    vec3 tint[4] = vec3[4](vec3(230.0, 40.0, 40.0) / 255.0, vec3(40.0, 200.0, 70.0) / 255.0,
                           vec3(50.0, 110.0, 255.0) / 255.0, vec3(240.0, 210.0, 30.0) / 255.0);
    float labelScale = max(1.0, floor(corner * 0.55 / 7.0));
    for (int k = 0; k < 4; ++k) {
        vec2 a = abs(q - at[k]);
        if ((a.x + a.y) / (corner * 1.6) <= 1.0) {
            c = tint[k];
            vec2 inward = vec2(at[k].x > 0.0 ? -1.0 : 1.0, at[k].y > 0.0 ? -1.0 : 1.0);
            vec2 label = at[k] + inward * corner * 0.5;
            vec2 cell = floor((q - label) / labelScale + vec2(2.5, 3.5));
            if (glyph(k + 1, cell) > 0.5) c = k == 3 ? vec3(0.0) : vec3(1.0);
        }
    }

    // A frame on the outermost pixels: whatever is cropped shows
    if (q.x < 1.0 || q.y < 1.0 || q.x >= W - 1.0 || q.y >= H - 1.0) c = vec3(1.0);

    // The size, on a dark plate in the middle
    if (showSize) {
        int w = int(W + 0.5), h = int(H + 0.5);
        int chars = digitCount(w) + 3 + digitCount(h);
        float scale = max(1.0, floor(unit * 0.07 / 7.0));
        vec2 text = vec2(float(chars) * 6.0 - 1.0, 7.0) * scale;
        vec2 extent = text * 0.5 + unit * 0.035;
        float plate = roundedBox(q, center, extent, unit * 0.015);
        c = mix(c, vec3(0.0), (200.0 / 255.0) * clamp(0.5 - plate, 0.0, 1.0));
        vec2 t = (q - (center - text * 0.5)) / scale;
        if (t.x >= 0.0 && t.y >= 0.0 && t.x < text.x / scale && t.y < 7.0) {
            int i = int(floor(t.x / 6.0));
            if (glyph(sizeChar(w, h, i), vec2(t.x - float(i) * 6.0, t.y)) > 0.5) c = vec3(1.0);
        }
    }

    // Motion: a sweep turning clockwise in the circle once every 4 s with a fading trail,
    // and a white block going to and fro in its band
    if (motion) {
        vec2 d = p - vec2(center.x, H - center.y);
        if (length(d) < radius) {
            float s = mod(-TIME * 1.5707963, 6.2831853);
            float behind = mod(atan(d.y, d.x) - s, 6.2831853);
            c = mix(c, vec3(0.25, 1.0, 0.45), 0.6 * exp(-behind * 2.5));
        }
        float bw = band.z * 0.06;
        float x = band.x + (band.z - bw) * (0.5 + 0.5 * sin(TIME * 2.0));
        if (q.y >= band.y && q.y <= band.y + band.w && q.x >= x && q.x <= x + bw) c = vec3(1.0);
    }

    gl_FragColor = vec4(c, 1.0);
}
