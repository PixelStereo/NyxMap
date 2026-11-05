#!/usr/bin/env python3
"""Sub Pressure: 30 s of slow UK dub, at 82 BPM: a hip hop beat, a ragga's dembow, a skank and a walking sub, for pulses of lines and 3D lights.

    python3 examples/sub-pressure/source/build_show.py [--autoplay]

Everything that sounds is a Faust instrument of NyxMap, in its audio callback, played by one timeline: no sound file. Everything
that shows is lines and lights: a 3D tunnel of glowing lines, spots in its fog, and NyxMap's own shaders (found in its library,
not copied). Each voice's gate drives its own picture, so a pulse falls with its note.
"""
import copy
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
W, H = 1920, 1080
BPM = 82
BEAT = 60 / BPM
BAR = 4 * BEAT
S16 = BEAT / 4
SWING = 0.16  # even 16ths, late by this much of one
TOTAL = 30.0
A, BREAK, B, LAST = 2, 6, 7, 9.5  # bars: the riddim, its break, the riddim again, the last hit
layers, tracks, interactions = [], {}, []
_id = [100]


def new_id():
    _id[0] += 1
    return str(_id[0])


def key(layer, param, t, value, curve="hold"):
    tracks.setdefault((layer, param), []).append([round(max(0.0, t), 4), value] + ([curve] if curve is not None else []))


def at(bar, step=0.0):
    return bar * BAR + (step + (SWING if int(step) & 1 else 0)) * S16


# ---------------------------------------------------------------------------------------------------------------------
# The sound: each voice a layer, its instrument, Faust's envelope of its choice, its Audio FX
# ---------------------------------------------------------------------------------------------------------------------
(HERE / "audio").mkdir(exist_ok=True)


def voice(name, kind, freq=110.0, ratio=1.0, index=0.0, gain=0.3, attack=0.002, decay=0.1, sustain=0.0, release=0.03, envelope="adsre", volume=1.0, fx=()):
    recipe = f"audio/{name.lower().replace(' ', '-')}.nyxaudio"
    (HERE / recipe).write_text(json.dumps({"version": 1, "type": kind, "frequency": freq, "ratio": ratio, "index": index, "gain": gain, "attack": attack, "decay": decay,
                                           "sustain": sustain, "release": release, "gate": 0}) + "\n")
    lid = new_id()
    effects = [{"id": f"{lid}-{k}", "kind": k, "enabled": True, "wet": wet, "values": values} for k, values, wet in fx]
    layers.append({"id": lid, "name": name, "enable": True, "locked": False, "viewports": {}, "fx": [], "play": True, "source": {"type": "audio", "path": recipe, "relative_path": recipe},
                   "audio_fx": effects, "params": {"play": True, "play_mode": "loop", "volume": volume, "audio_fx/enable": True, "synth/envelope": envelope}})
    key(lid, "synth/gate", 0, 0)
    return lid


def lp(c, r=0.8):
    return ("lowpass", {"cutoff": c, "resonance": r}, 1)


def hp(c, r=0.8):
    return ("highpass", {"cutoff": c, "resonance": r}, 1)


def bp(c, r=1.0):
    return ("bandpass", {"cutoff": c, "resonance": r}, 1)


def drive(d, tone=3000):
    return ("distortion", {"drive": d, "tone": tone}, 1)


def room(size=0.85, wet=0.35):
    return ("reverb", {"room": size, "damping": 0.4}, wet)


def echo(ms, fb, wet=0.4):
    return ("delay", {"time_ms": ms, "feedback": fb}, wet)


E1, G1, A1, B1, E2, E3, G3, A3, B3, C4, E4 = 41.2, 49.0, 55.0, 61.74, 82.41, 164.81, 196.0, 220.0, 246.94, 261.63, 329.63
DOTTED = BEAT * 750  # a dotted eighth, in ms: the dub's echo

KICK = voice("Kick", "fm", 55, 1, 0, 0.42, decay=0.34, fx=[drive(0.1, 6000)])
CLICK = voice("Kick Click", "white", gain=0.06, decay=0.006, fx=[hp(3000)])
SNARE = voice("Snare", "pink", gain=0.34, decay=0.24, fx=[bp(1900, 0.7), echo(DOTTED, 0.62, 0), room(0.8, 0.32)])
CRACK = voice("Snare Crack", "white", gain=0.11, decay=0.05, fx=[hp(1800), room(0.7, 0.2)])
BODY = voice("Snare Body", "fm", 180, 1, 0.2, 0.15, decay=0.13)
HAT = voice("Hat", "white", gain=0.07, decay=0.03, fx=[hp(8000, 0.9)])
OPEN = voice("Open Hat", "white", gain=0.06, decay=0.2, fx=[hp(7000, 0.9), room(0.4, 0.12)])
RIM = voice("Rim", "fm", 1250, 2.41, 2.0, 0.05, decay=0.026, fx=[room(0.55, 0.2)])
SUB = voice("Sub", "fm", E1, 1, 0.08, 0.32, attack=0.008, decay=0.2, sustain=1, release=0.07, envelope="adsr")
# The sub an octave up, for what cannot play 41 Hz
GROWL = voice("Sub Growl", "fm", E2, 1, 1.3, 0.05, attack=0.008, decay=0.2, sustain=1, release=0.07, envelope="adsr", fx=[drive(0.35, 1000), lp(420, 0.8)])
# The skank: a minor chord on each offbeat, short, its echo opened by hand
STAB_R = voice("Skank Root", "fm", E3, 2.0, 1.1, 0.17, attack=0.003, decay=0.1, fx=[bp(1000, 0.7), echo(DOTTED, 0.62, 0.04), room(0.75, 0.3)])
STAB_T = voice("Skank Third", "fm", G3, 2.0, 1.1, 0.14, attack=0.003, decay=0.1, fx=[bp(1200, 0.7), echo(DOTTED, 0.62, 0.04), room(0.75, 0.3)])
STAB_F = voice("Skank Fifth", "fm", B3, 2.0, 1.1, 0.14, attack=0.003, decay=0.1, fx=[bp(1400, 0.7), echo(DOTTED, 0.62, 0.04), room(0.75, 0.3)])
MELODICA = voice("Melodica", "fm", 440, 1.0, 1.3, 0.11, attack=0.03, decay=0.1, sustain=0.8, release=0.09, envelope="adsr", fx=[bp(1500, 0.6), echo(DOTTED, 0.5, 0.35), room(0.85, 0.42)])
HORN = voice("Horn", "fm", 523.25, 1.003, 5.5, 0.05, attack=0.005, decay=0.05, sustain=0.9, release=0.03, envelope="adsr", fx=[drive(0.3, 3000), bp(1700, 0.7), echo(DOTTED, 0.55, 0.3), room(0.8, 0.3)])
SIREN = voice("Siren", "fm", 470, 1, 0.5, 0.07, attack=0.02, decay=0.1, sustain=1, release=0.2, envelope="adsr", fx=[bp(1000, 0.7), echo(DOTTED, 0.68, 0.55), room(0.9, 0.45)])
RISE = voice("Riser", "white", gain=0.1, attack=0.01, sustain=1, release=0.05, envelope="adsr", fx=[bp(400, 1.6), room(0.75, 0.3)])
AIR = voice("Air", "pink", gain=0.03, attack=2.5, decay=0.1, sustain=1, release=1.5, envelope="adsr", fx=[bp(900, 2.2), room(0.95, 0.6)])
BOOM = voice("Boom", "fm", 120, 1, 0, 0.3, decay=2.0, fx=[room(0.9, 0.22)])
SOUND_LAYERS = layers[:]
layers.clear()


def hit(layer, t, length=0.03):
    key(layer, "synth/gate", t, 1)
    key(layer, "synth/gate", t + length, 0)


def kick(t):
    hit(KICK, t)
    hit(CLICK, t, 0.01)


def snare(t):
    hit(SNARE, t)
    hit(CRACK, t, 0.015)
    hit(BODY, t)


def sub(t, steps, freq):
    for lid, f in ((SUB, freq), (GROWL, freq * 2)):
        key(lid, "synth/frequency", t - 0.002, f)
        key(lid, "synth/gate", t, 1)
        key(lid, "synth/gate", t + steps * S16 - 0.02, 0)


# ---------------------------------------------------------------------------------------------------------------------
# The pictures: lines and lights, nothing else. Top first
# ---------------------------------------------------------------------------------------------------------------------
def generator(name, sh, params=None, opacity=0.0):
    lid = new_id()
    path = f"{sh}.fs"  # from NyxMap's library
    p = {"opacity": opacity, "blend_mode": "add"}
    p.update({f"param/{k}": v for k, v in (params or {}).items()})
    layers.append({"id": lid, "name": name, "enable": True, "locked": False, "viewports": {}, "fx": [], "source": {"type": "isf", "path": path, "width": W, "height": H}, "params": p})
    return lid


THREAD = generator("Thread", "Fil", {"x": 0.5, "reach": 1, "amp": 0.05, "width": 0.0008, "glow": 0.2, "count": 1, "spread": 0.012}, opacity=1)
BARS = generator("Snare Bars", "Light Bars", {"count": 9, "width": 0.05, "speed": 0, "angle": 0, "mirror": True, "spread": 1})
SCAN = generator("Skank Lines", "Lines", {"spacing": 0.09, "line_width": 0.0035, "angle": 0, "shift": 0, "color1/r": 1, "color1/g": 1, "color1/b": 1, "color1/a": 1, "color2/r": 0, "color2/g": 0, "color2/b": 0, "color2/a": 0})
WEB = generator("Hat Lines", "Random Lines", {"lineCount": 14, "lineWidth": 0.0012, "randomSeed": 1, "wobbleAmount": 0, "hueRange": 0, "colorSaturation": 0})
WAVES = generator("Bass Lines", "Waveform Lines", {"lines": 34, "amplitude": 1.1, "depth": 0.8, "tilt": 0.4, "thickness": 1.1})

STAGE = new_id()
layers.append({"id": STAGE, "name": "Tunnel", "enable": True, "locked": False, "viewports": {}, "group": True, "scene3d": True, "collapsed": True, "source": {"type": "none"}, "fx": [],
               "params": {"opacity": 1, "blend_mode": "normal", "camera/distance": 15, "camera/fov": 50, "camera/pitch": 0, "camera/yaw": 0, "camera/target/x": 0, "camera/target/y": 0, "camera/target/z": -12,
                          "scene/background/r": 0, "scene/background/g": 0, "scene/background/b": 0, "scene/background/a": 1, "scene/studio": 0, "scene/ambient_intensity": 0.0, "scene/exposure": 1.1,
                          "scene/glow": 1.3, "scene/glow_threshold": 0.55, "scene/glow_size": 0.8, "scene/contrast": 1.15, "scene/saturation": 1, "scene/vignette": 0.45, "scene/reflections": 1,
                          "scene/fog/density": 0.09, "scene/fog/base": -3, "scene/fog/falloff": 0.05, "scene/fog/reach": 40, "scene/fog/color/r": 0, "scene/fog/color/g": 0, "scene/fog/color/b": 0}})


def model(name, shape, parent=STAGE, position=(0, 0, 0), rotation=(0, 0, 0), axes=(1, 1, 1), scale=1.0, color=(0, 0, 0), metal=0.0, rough=0.5, unlit=True):
    p = {"model/fit": True, "model/detail": 1, "model/unlit": unlit, "model/scale/all": scale, "model/material/metal": metal, "model/material/rough": rough}
    for i, a in enumerate("xyz"):
        p[f"model/position/{a}"], p[f"model/rotation/{a}"], p[f"model/scale/{a}"] = position[i], rotation[i], axes[i]
    for i, a in enumerate("rgb"):
        p[f"model/material/color/{a}"] = color[i]
    lid = new_id()
    layers.append({"id": lid, "name": name, "enable": True, "locked": False, "viewports": {}, "parent": parent, "source": {"type": "model", "path": f"/NyxMap/3D/{shape}"}, "fx": [], "params": p})
    return lid


def merge(name, position=(0, 0, 0)):
    lid = new_id()
    layers.append({"id": lid, "name": name, "enable": True, "locked": False, "viewports": {}, "group": True, "merge3d": True, "collapsed": True, "parent": STAGE, "source": {"type": "none"}, "fx": [],
                   "params": {"merge/position/x": position[0], "merge/position/y": position[1], "merge/position/z": position[2], "merge/scale/all": 1}})
    return lid


def spot(name, position, look, color, intensity=0.0, angle=9.0, rays=1.6):
    p = {"light/type": "spot", "light/intensity": intensity, "light/level": 1, "light/angle": angle, "light/falloff": 0.25, "light/softness": 2, "light/rays": rays, "light/shadow": False, "light/lookat": True}
    for i, a in enumerate("xyz"):
        p[f"light/position/{a}"], p[f"light/lookpoint/{a}"] = position[i], look[i]
    for i, a in enumerate("rgb"):
        p[f"light/color/{a}"] = color[i]
    lid = new_id()
    layers.append({"id": lid, "name": name, "enable": True, "locked": False, "viewports": {}, "parent": STAGE, "source": {"type": "light"}, "fx": [], "params": p})
    return lid


FRAMES = 12
GAP = 2.3
HALF_W, HALF_H = 4.4, 2.5
THICK = 0.012
frames = []
for k in range(FRAMES):
    group = merge(f"Frame {k + 1}", (0, 0, -k * GAP))
    bars = [model(f"Frame {k + 1} Top", "Cube", group, (0, HALF_H, 0), axes=(2 * HALF_W, THICK, THICK)), model(f"Frame {k + 1} Bottom", "Cube", group, (0, -HALF_H, 0), axes=(2 * HALF_W, THICK, THICK)),
            model(f"Frame {k + 1} Left", "Cube", group, (-HALF_W, 0, 0), axes=(THICK, 2 * HALF_H, THICK)), model(f"Frame {k + 1} Right", "Cube", group, (HALF_W, 0, 0), axes=(THICK, 2 * HALF_H, THICK))]
    frames.append((group, bars))
FLOOR = model("Floor", "Plane", position=(0, -2.9, -12), rotation=(-90, 0, 0), scale=40, color=(0.03, 0.03, 0.035), metal=0.9, rough=0.22, unlit=False)
WHITE, COLD, RED = (1, 1, 1), (0.72, 0.88, 1), (1, 0.04, 0.05)
KICK_LIGHTS = [spot("Kick Left", (-4, 2.4, 3), (1.2, -2.8, -14), COLD, angle=8), spot("Kick Right", (4, 2.4, 3), (-1.2, -2.8, -14), COLD, angle=8)]
SNARE_LIGHTS = [spot(f"Snare {k + 1}", (x, 2.45, -3 - 5 * k), (-x, -2.8, -8 - 5 * k), WHITE, angle=10) for k, x in enumerate((-3.6, 3.6, -3.6, 3.6))]
WOB_LIGHTS = [spot(f"Bass {k + 1}", (x, -2.4, -2 - 6 * k), (x * 0.2, 2.5, -7 - 6 * k), RED, angle=13, rays=2.2) for k, x in enumerate((3.8, -3.8, 3.8))]
HAT_LIGHT = spot("Rim", (0, 2.45, -1), (0, -2.8, -22), WHITE, angle=4, rays=1.2)
HORN_LIGHT = spot("Horn", (0, 2.4, 2), (0, 0, -24), WHITE, angle=22, rays=2)


def colour(bars, t, level, curve="hold"):
    for lid in bars:
        for a in "rgb":
            key(lid, f"model/material/color/{a}", t, round(level, 3), curve)


def wave(t, strength=1.0, back=False, base=0.05, fall=0.5, speed=0.045):
    """A pulse runs down the tunnel: the frames light one after the other."""
    for k, (group, bars) in enumerate(frames):
        when = t + (FRAMES - 1 - k if back else k) * speed
        colour(bars, when, base + strength * (1 - 0.35 * k / FRAMES) * 2.2)
        colour(bars, when + fall, base, 0)
        key(group, "merge/scale/all", when, round(1 + 0.05 * strength, 3))
        key(group, "merge/scale/all", when + fall, 1, 0)


for group, bars in frames:
    colour(bars, 0, 0.0)
    key(group, "merge/scale/all", 0, 1)


# ---------------------------------------------------------------------------------------------------------------------
# The score
# ---------------------------------------------------------------------------------------------------------------------
key(AIR, "synth/gate", 0, 1)
key(AIR, "synth/gate", TOTAL - 1.6, 0)
for bar, cut in ((0, 500), (A, 1800), (BREAK, 3200), (B, 1400), (10.25, 300)):
    key(AIR, f"audio_fx/{AIR}-bandpass/cutoff", bar * BAR, cut, 0)
key(THREAD, "param/reach", 0, 0.12, "hold")
key(THREAD, "param/reach", at(A) - BEAT, 1.0, 0)

# The riddim, its two bars: the bass walks, Em then Am
LINE_1 = [(0, 5, E1), (6, 2, G1), (8, 3, A1), (12, 2, G1), (14, 2, E1)]
LINE_2 = [(0, 5, E1), (6, 2, G1), (8, 2, A1), (10, 2, B1), (12, 4, A1)]
EM, AM = (E3, G3, B3), (A3, C4, E4)


def skank(bar, chord, steps=(2, 6, 10, 14)):
    for s in steps:
        for lid, f in zip((STAB_R, STAB_T, STAB_F), chord):
            key(lid, "synth/frequency", at(bar, s) - 0.002, f)
            hit(lid, at(bar, s), 0.05)


def throw(layer, t, length=S16 * 2, dry=0.04, wet=0.8):
    """A dub's hand on the echo: this one note goes into it, and repeats."""
    key(layer, f"audio_fx/{layer}-delay/wet", t - 0.004, wet)
    key(layer, f"audio_fx/{layer}-delay/wet", t + length, dry)


def melodica(bar, phrase):
    for s, n, f in phrase:
        key(MELODICA, "synth/frequency", at(bar, s) - 0.002, f)
        key(MELODICA, "synth/gate", at(bar, s), 1)
        key(MELODICA, "synth/gate", at(bar, s + n) - 0.03, 0)


def horn(t, length=0.11):
    hit(HORN, t, length)


for lid in (STAB_R, STAB_T, STAB_F):
    key(lid, f"audio_fx/{lid}-delay/wet", 0, 0.04)
key(SNARE, f"audio_fx/{SNARE}-delay/wet", 0, 0)

# The intro: a line, a skank thrown into its echo, the bass on its own
skank(0, EM, (2, 10))
throw(STAB_R, at(0, 10))
throw(STAB_T, at(0, 10))
throw(STAB_F, at(0, 10))
skank(1, AM, (2, 6, 10))
for bar in (0, 1):
    wave(at(bar, 0), 0.4 + 0.2 * bar, base=0.03 + 0.02 * bar, fall=1.4, speed=0.06)
    wave(at(bar, 8), 0.3 + 0.2 * bar, back=True, base=0.03 + 0.02 * bar, fall=1.0, speed=0.05)
for s, n, f in LINE_1:
    sub(at(1, s), n, f)
for s in (3, 6, 11, 14):
    hit(RIM, at(1, s), 0.012)
# The siren, then the riser
key(SIREN, "synth/gate", at(0, 6), 1)
key(SIREN, "synth/gate", at(1, 2), 0)
n, t = 0, at(0, 6)
while t < at(1, 2):
    key(SIREN, "synth/frequency", t, 980 if n % 2 == 0 else 470, 0)
    t += BEAT / 2
    n += 1
key(RISE, "volume", 0, 0)
for a, b in ((at(1, 8), at(A) - S16), (at(BREAK, 8), at(B) - S16)):
    key(RISE, "synth/gate", a, 1)
    key(RISE, "synth/gate", b, 0)
    key(RISE, f"audio_fx/{RISE}-bandpass/cutoff", a, 350, "hold")
    key(RISE, f"audio_fx/{RISE}-bandpass/cutoff", b, 6000, 0)
    key(RISE, "volume", a, 0, "hold")
    key(RISE, "volume", b, 1, 0)
    key(RISE, "volume", b + 0.01, 0, "hold")

# The riddim: a hip hop beat, swung, under a ragga's dembow on the rim, the skank on the offbeats
LAST_BAR = int(LAST)
for bar in [b for b in range(A, BREAK)] + [b for b in range(B, LAST_BAR + 1)]:
    second = bar >= B
    end = bar == LAST_BAR  # it stops at its half: the last hit
    kicks = ((0, 7, 10) if bar % 2 == 0 else (0, 6, 10, 15 if second else 11))
    for s in kicks:
        if end and s >= 8:
            continue
        kick(at(bar, s))
        wave(at(bar, s), 1 if s == 0 else 0.6)
    for s in (4, 12):
        if end and s >= 8:
            continue
        snare(at(bar, s))
        wave(at(bar, s), 0.9, back=True, fall=0.3, speed=0.024)
    for s in (3, 6, 11, 14):
        if not (end and s >= 8):
            hit(RIM, at(bar, s), 0.012)
    for s in range(0, 8 if end else 16, 1 if second else 2):
        if s != 14:
            hit(HAT, at(bar, s), 0.012)
    if bar % 2 == 1 and not end:
        hit(OPEN, at(bar, 14), 0.05)
    skank(bar, AM if bar % 4 == 3 else EM, (2, 6) if end else (2, 6, 10, 14))
    for s, n, f in LINE_2 if bar % 2 else LINE_1:
        if not (end and s >= 8):
            sub(at(bar, s), n, f)
# A snare and a skank thrown into the echo, once in a while: the dub's hand
for bar, s in ((3, 12), (5, 12), (8, 12)):
    throw(SNARE, at(bar, s), S16 * 1.5, dry=0, wet=0.75)
for lid in (STAB_R, STAB_T, STAB_F):
    for bar, s in ((3, 14), (5, 6), (7, 14), (8, 14)):
        throw(lid, at(bar, s))
E4_, G4, A4, B4, D5 = 329.63, 392.0, 440.0, 493.88, 587.33
melodica(4, [(0, 3, B4), (4, 2, A4), (6, 2, G4), (8, 6, E4_)])
melodica(5, [(8, 2, G4), (10, 2, A4), (12, 4, B4)])
melodica(7, [(0, 6, B4), (8, 2, D5), (10, 2, B4), (12, 4, A4)])
melodica(8, [(0, 4, G4), (6, 8, E4_)])
# The break: the bass alone, the horn of the sound system, the roll
sub(at(BREAK, 0), 8, E1)
wave(at(BREAK, 0), 0.5, fall=1.6, speed=0.08)
for s in (0, 2, 4):
    horn(at(BREAK, s))
horn(at(BREAK, 7), S16 * 0.9)
for s in range(8, 16):
    snare(at(BREAK, s))
    key(SNARE, "volume", at(BREAK, s), round(0.2 + 0.11 * (s - 8), 2))
    wave(at(BREAK, s), 0.16 + 0.11 * (s - 8), fall=0.14, speed=0.016)
key(SNARE, "volume", at(B), 1)
for s in (0, 2, 4):
    horn(at(B, s))
# The last hit, and the line alone in its tail
END_HIT = LAST * BAR
kick(END_HIT)
snare(END_HIT)
hit(BOOM, END_HIT, 0.05)
key(BOOM, "synth/frequency", END_HIT - 0.002, 130)
key(BOOM, "synth/frequency", END_HIT + 0.5, 36, 0)
for lid, f in zip((STAB_R, STAB_T, STAB_F), EM):
    key(lid, "synth/frequency", END_HIT - 0.002, f)
    hit(lid, END_HIT, 0.05)
    throw(lid, END_HIT, wet=0.9)
wave(END_HIT, 1.5, fall=1.8, speed=0.06, base=0)
key(THREAD, "param/reach", END_HIT + 0.6, 1.0, "hold")
key(THREAD, "param/reach", TOTAL - 0.1, 0.0, 0)

# What changes with each of them: the bars' angle and number at each snare, the stab's, the hats' web
snares = sorted({k[0] for k in tracks[(SNARE, "synth/gate")] if k[1] == 1})
SHAPES = [(0, 9), (90, 5), (0, 15), (28, 7), (-28, 7), (90, 13), (0, 3), (45, 11)]
for n, t in enumerate(snares):
    angle, count = SHAPES[n % len(SHAPES)]
    key(BARS, "param/angle", t - 0.002, angle)
    key(BARS, "param/count", t - 0.002, count)
stabs = sorted({k[0] for k in tracks[(STAB_R, "synth/gate")] if k[1] == 1})
for n, t in enumerate(stabs):
    key(SCAN, "param/angle", t - 0.002, (0, 90, 45, -45)[n % 4])
    key(SCAN, "param/spacing", t - 0.002, (0.09, 0.05, 0.16, 0.03)[n % 4])
hats = sorted({k[0] for k in tracks[(HAT, "synth/gate")] if k[1] == 1})
for n, t in enumerate(hats):
    key(WEB, "param/randomSeed", t - 0.002, float(n % 37 + 1))

# The camera: drifting between its cuts, on the bars that change the piece
#        bar   distance pitch yaw  target y, z   fov, then where it has drifted to by the next
SHOTS = [(0, 15, 0, 0, 0, -12, 50), (A, 11, 0, 0, 0, -12, 62), (3, 16, 8, 26, 0, -12, 42), (4, 10, -3, -14, 0, -10, 70), (5, 20, 22, 40, 0, -12, 34), (BREAK, 17, 0, 0, 0, -12, 28),
         (B, 8.5, 0, 0, 0, -9, 82), (8, 14, -4, -32, 0, -12, 58), (8.75, 9, 4, 18, 0, -8, 78), (LAST, 13, 0, 0, 0, -12, 60)]
for n, (bar, dist, pitch, yaw, ty, tz, fov) in enumerate(SHOTS):
    t = bar * BAR
    until = SHOTS[n + 1][0] * BAR - 0.02 if n + 1 < len(SHOTS) else TOTAL
    key(STAGE, "camera/distance", t, dist)
    key(STAGE, "camera/distance", until, dist * 0.82, 0)
    key(STAGE, "camera/pitch", t, pitch)
    key(STAGE, "camera/yaw", t, yaw)
    key(STAGE, "camera/yaw", until, yaw + (6 if n % 2 else -6), 0)
    key(STAGE, "camera/target/y", t, ty)
    key(STAGE, "camera/target/z", t, tz)
    key(STAGE, "camera/fov", t, fov)
key(STAGE, "scene/fog/density", 0, 0.05, 0)
key(STAGE, "scene/fog/density", A * BAR, 0.09, 0)
key(STAGE, "scene/fog/density", B * BAR, 0.16, 0)
key(STAGE, "scene/fog/density", TOTAL, 0.03, 0)


# ---------------------------------------------------------------------------------------------------------------------
# Each voice's gate, an envelope on what it lights: a pulse falls with its note
# ---------------------------------------------------------------------------------------------------------------------
def follow(source, layer, param, low, high, decay, attack=0.001, sustain=0.0):
    n = len(interactions) + 1
    interactions.append({"id": str(n), "name": f"Gate → {param}", "enabled": True, "source": "internal", "source_layer": source, "source_param": "synth/gate", "layer": layer, "param": param, "master": 1, "takeover": "jump",
                         "filters": [{"type": "envelope", "a": attack, "b": decay, "c": sustain, "d": 0.04}, {"type": "remap", "a": 0, "b": 1, "c": low, "d": high}]})


follow(KICK, KICK, "synth/frequency", 55, 190, 0.04)
follow(BODY, BODY, "synth/frequency", 165, 300, 0.03)
follow(KICK, GROWL, "volume", 1, 0.35, 0.16)  # It gives way to the kick
for lid in KICK_LIGHTS:
    follow(KICK, lid, "light/intensity", 0, 900, 0.4)
for lid in SNARE_LIGHTS:
    follow(SNARE, lid, "light/intensity", 0, 1400, 0.28)
for lid in (WOB_LIGHTS[0], WOB_LIGHTS[2]):
    follow(SUB, lid, "light/intensity", 0, 300, 0.3, attack=0.008)
follow(MELODICA, WOB_LIGHTS[1], "light/intensity", 0, 260, 0.25, attack=0.02, sustain=0.35)
follow(RIM, HAT_LIGHT, "light/intensity", 0, 500, 0.08)
follow(OPEN, HAT_LIGHT, "light/level", 1, 2.5, 0.2)
follow(HORN, HORN_LIGHT, "light/intensity", 0, 900, 0.12, sustain=0.6)
follow(KICK, THREAD, "param/glow", 0.2, 2.8, 0.35)
follow(SUB, THREAD, "param/amp", 0.03, 0.26, 0.5, attack=0.01, sustain=0.5)
follow(SNARE, BARS, "opacity", 0, 1, 0.2)
follow(STAB_R, SCAN, "opacity", 0, 0.75, 0.13)
follow(HAT, WEB, "opacity", 0, 0.5, 0.06)
follow(GROWL, WAVES, "opacity", 0, 0.95, 0.4, attack=0.01, sustain=0.22)
follow(KICK, STAGE, "scene/glow", 1.3, 3.2, 0.28)
follow(SNARE, STAGE, "scene/exposure", 1.1, 2.2, 0.16)

layers.extend(SOUND_LAYERS)
timeline_tracks = []
for (layer, param), keys in tracks.items():
    keys.sort(key=lambda k: k[0])
    timeline_tracks.append({"layer": layer, "param": param, "keys": keys})
timelines = [{"id": "1", "name": "Sub Pressure", "duration": TOTAL, "loop": "once", "tracks": timeline_tracks}]
states = [dict(copy.deepcopy(layer), included=True) for layer in layers]
snapshot = {"id": "1", "name": "Ready", "fade": 0, "layers": states, "composition": {"included": True, "opacity": 1, "volume": 0.8}, "interactions": [{"id": i["id"], "included": True, "enabled": True} for i in interactions]}
steps = [{"text": "GO", "snapshot": "1", "description": "All of it plays from here, as it is made", "continue": "follow"}, {"text": "Sub Pressure", "timeline": "1", "action": "play"}]
project = {"app": "NyxMap", "format_version": 1, "composition": {"width": W, "height": H, "fps": 60, "speed": 1}, "audio": {"volume": 0.8}, "layers": layers, "interactions": interactions, "timelines": timelines, "presets": [], "snapshots": [snapshot], "snapshot_tree": ["1"],
           "sequences": [{"id": "1", "name": "Sub Pressure", "loop": False, "steps": steps}], "current_sequence": 0, "startup_sequence": 0, "startup_step": 0, "startup_go": "--autoplay" in sys.argv, "ui": {"selected_layer": 0}}
(HERE / "Sub Pressure.nyxmap").write_text(json.dumps(project, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
print(f"{len(layers)} layers, {len(interactions)} interactions, {sum(len(t['keys']) for t in timeline_tracks)} keys on {len(timeline_tracks)} tracks, {TOTAL:.1f} s")
