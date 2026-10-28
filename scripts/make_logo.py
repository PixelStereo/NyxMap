#!/usr/bin/env python3
"""Draws the NyxMap logo as SVG (assets/nyxmap.svg, assets/nyxmap-app.svg); no font is needed, the wordmark is drawn
with strokes. The rasters (assets/nyxmap*.png, NyxMap.icns, NyxMap.ico) are rendered from these two files.

A crescent moon (Nyx, the night) lit inside a warped projection mesh with its four corner-pin handles (the mapping).
  nyxmap.svg      full-bleed square with the wordmark (About box, window icon fallback)
  nyxmap-app.svg  rounded app icon, no text (readable down to 16 px)
"""
import math, pathlib, sys

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent / "assets")


def f(v): return f"{v:.1f}"


def mesh(quad, warp, n=4, steps=28):
    """The mesh over the quad (TL, TR, BR, BL), bent by `warp` (a push at its middle): polylines, borders first."""
    (x0, y0), (x1, y1), (x2, y2), (x3, y3) = quad

    def at(u, v):
        x = (1 - v) * ((1 - u) * x0 + u * x1) + v * ((1 - u) * x3 + u * x2)
        y = (1 - v) * ((1 - u) * y0 + u * y1) + v * ((1 - u) * y3 + u * y2)
        bump = math.sin(math.pi * u) * math.sin(math.pi * v)
        return x + warp[0] * bump, y + warp[1] * bump

    def line(fixed, along):
        pts = [at(t / steps, fixed) if along == "u" else at(fixed, t / steps) for t in range(steps + 1)]
        return "M" + " L".join(f"{f(x)},{f(y)}" for x, y in pts)

    inner = [line(k / n, a) for k in range(1, n) for a in "uv"]
    border = [line(0, "u"), line(1, "u"), line(0, "v"), line(1, "v")]
    outline = [at(0, 0), at(1, 0), at(1, 1), at(0, 1)]
    return inner, border, outline


def crescent(c, r, c2, r2, count=160):
    """The part of the disc (c, r) outside the disc (c2, r2), as a polygon."""
    cx, cy = c
    dx, dy = c2[0] - cx, c2[1] - cy
    d = math.hypot(dx, dy)
    a = (r * r - r2 * r2 + d * d) / (2 * d)
    h = math.sqrt(r * r - a * a)
    mx, my = cx + a * dx / d, cy + a * dy / d
    p1 = (mx + h * dy / d, my - h * dx / d)
    p2 = (mx - h * dy / d, my + h * dx / d)
    ang = lambda p, o: math.atan2(p[1] - o[1], p[0] - o[0])
    pts = []
    a1, a2 = ang(p1, c), ang(p2, c)  # the outer arc, the long way round (away from the cut)
    away = math.atan2(-dy, -dx)
    if (a2 - a1) % (2 * math.pi) > (away - a1) % (2 * math.pi) > 0:  # p1 -> p2 counter-clockwise passes `away`
        sweep = (a2 - a1) % (2 * math.pi)
    else:
        sweep = -((a1 - a2) % (2 * math.pi))
    for i in range(count + 1):
        t = a1 + sweep * i / count
        pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    b1, b2 = ang(p2, c2), ang(p1, c2)  # the inner arc back, inside the outer disc (towards the first centre)
    toward = math.atan2(-dy, -dx) + math.pi - math.pi  # direction from c2 to c
    toward = math.atan2(cy - c2[1], cx - c2[0])
    s1 = (b2 - b1) % (2 * math.pi)
    sweep2 = s1 if (toward - b1) % (2 * math.pi) < s1 else -((b1 - b2) % (2 * math.pi))
    for i in range(count + 1):
        t = b1 + sweep2 * i / count
        pts.append((c2[0] + r2 * math.cos(t), c2[1] + r2 * math.sin(t)))
    return "M" + " L".join(f"{f(x)},{f(y)}" for x, y in pts) + " Z"


def sparkle(cx, cy, s):
    k = s * 0.18
    pts = [(cx, cy - s), (cx + k, cy - k), (cx + s, cy), (cx + k, cy + k), (cx, cy + s), (cx - k, cy + k), (cx - s, cy), (cx - k, cy - k)]
    return "M" + " L".join(f"{f(x)},{f(y)}" for x, y in pts) + " Z"


def letters(text, x, y, w, h, gap):
    """The wordmark in strokes: one path per letter."""
    def P(*cmds): return " ".join(cmds)
    out = []
    for ch in text:
        if ch == "N": d = f"M{x},{y+h} L{x},{y} L{x+w},{y+h} L{x+w},{y}"
        elif ch == "Y": d = f"M{x},{y} L{x+w/2},{y+h*0.48} L{x+w},{y} M{x+w/2},{y+h*0.48} L{x+w/2},{y+h}"
        elif ch == "X": d = f"M{x},{y} L{x+w},{y+h} M{x+w},{y} L{x},{y+h}"
        elif ch == "M": d = f"M{x},{y+h} L{x},{y} L{x+w/2},{y+h*0.62} L{x+w},{y} L{x+w},{y+h}"
        elif ch == "A": d = f"M{x},{y+h} L{x+w/2},{y} L{x+w},{y+h} M{x+w*0.2},{y+h*0.66} L{x+w*0.8},{y+h*0.66}"
        elif ch == "P":
            r = h * 0.28
            d = f"M{x},{y+h} L{x},{y} L{x+w-r},{y} A{r},{r} 0 0 1 {x+w-r},{y+2*r} L{x},{y+2*r}"
        else: raise ValueError(ch)
        out.append((ch, d))
        x += w + gap
    return out


def svg(app):
    W = 1024
    quad = [(205, 255), (822, 200), (858, 712), (160, 752)] if app else [(200, 150), (828, 118), (862, 596), (166, 628)]
    warp = (-8, 44) if app else (-6, 34)
    moon_c = (508, 470) if app else (510, 372)
    R = 188 if app else 168
    cut_c = (moon_c[0] + 74, moon_c[1] - 52)
    cut_r = R * 0.86
    inner, border, outline = mesh(quad, warp)
    quad_path = "M" + " L".join(f"{f(x)},{f(y)}" for x, y in outline) + " Z"
    o = []
    o.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {W}" width="{W}" height="{W}">')
    o.append("<defs>")
    o.append('<linearGradient id="bg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#07061c"/>'
             '<stop offset="0.55" stop-color="#150f3f"/><stop offset="1" stop-color="#2a1466"/></linearGradient>')
    o.append(f'<radialGradient id="halo" cx="{moon_c[0]}" cy="{moon_c[1]}" r="{R*2.1}" gradientUnits="userSpaceOnUse">'
             '<stop offset="0" stop-color="#8f7bff" stop-opacity="0.55"/><stop offset="1" stop-color="#8f7bff" stop-opacity="0"/></radialGradient>')
    o.append('<linearGradient id="moon" x1="0.1" y1="0.1" x2="0.9" y2="0.9"><stop offset="0" stop-color="#ffffff"/>'
             '<stop offset="0.5" stop-color="#d6c8ff"/><stop offset="1" stop-color="#7fd6ff"/></linearGradient>')
    o.append('<linearGradient id="grid" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#6ad7ff"/>'
             '<stop offset="1" stop-color="#a586ff"/></linearGradient>')
    o.append('<linearGradient id="accent" gradientUnits="userSpaceOnUse" x1="560" y1="0" x2="880" y2="0">'
             '<stop offset="0" stop-color="#6ad7ff"/><stop offset="1" stop-color="#a586ff"/></linearGradient>')
    o.append("</defs>")
    if app:
        o.append(f'<rect x="56" y="56" width="912" height="912" rx="210" ry="210" fill="url(#bg)"/>')
    else:
        o.append(f'<rect width="{W}" height="{W}" fill="url(#bg)"/>')
    for x, y, s, op in [(150, 140, 5, .8), (880, 120, 4, .6), (120, 560, 4, .5), (910, 540, 5, .7), (330, 90, 3, .6),
                        (700, 910 if app else 700, 4, .5), (250, 880 if app else 690, 3, .6)]:
        if app or y < 680 or x < 200: o.append(f'<circle cx="{x}" cy="{y}" r="{s}" fill="#ffffff" fill-opacity="{op}"/>')
    o.append(f'<path d="{quad_path}" fill="#7a68ff" fill-opacity="0.13"/>')
    o.append(f'<circle cx="{moon_c[0]}" cy="{moon_c[1]}" r="{R*2.1}" fill="url(#halo)"/>')
    for d in inner: o.append(f'<path d="{d}" fill="none" stroke="url(#grid)" stroke-width="5" stroke-opacity="0.7" stroke-linecap="round"/>')
    for d in border: o.append(f'<path d="{d}" fill="none" stroke="url(#grid)" stroke-width="9" stroke-linecap="round"/>')
    o.append(f'<path d="{crescent(moon_c, R, cut_c, cut_r)}" fill="url(#moon)"/>')
    o.append(f'<path d="{sparkle(moon_c[0] + R*0.55, moon_c[1] - R*0.18, 40)}" fill="#ffffff"/>')
    o.append(f'<path d="{sparkle(moon_c[0] + R*1.16, moon_c[1] + R*0.66, 18)}" fill="#cdbdff"/>')
    for x, y in outline:
        o.append(f'<circle cx="{f(x)}" cy="{f(y)}" r="22" fill="#0b0828" stroke="#6ad7ff" stroke-width="7"/>'
                 f'<circle cx="{f(x)}" cy="{f(y)}" r="9" fill="#ffffff"/>')
    if not app:
        w, h, gap = 92, 112, 34
        total = 6 * w + 5 * gap
        for i, (ch, d) in enumerate(letters("NYXMAP", (W - total) / 2, 770, w, h, gap)):
            stroke = "#ffffff" if i < 3 else "url(#accent)"
            o.append(f'<path d="{d}" fill="none" stroke="{stroke}" stroke-width="24" stroke-linecap="round" stroke-linejoin="round"/>')
    o.append("</svg>")
    return "\n".join(o) + "\n"


(OUT / "nyxmap.svg").write_text(svg(False))
(OUT / "nyxmap-app.svg").write_text(svg(True))
print("wrote", OUT / "nyxmap.svg", OUT / "nyxmap-app.svg")
