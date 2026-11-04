#!/usr/bin/env python3
"""Monolith Live: rhythms and drones synthesized by NyxMap as it plays, a picture that changes at every beat, one thread.

    python3 examples/monolith-live/source/build_show.py [--autoplay]

Everything that sounds runs in NyxMap's audio callback: Faust instruments with their envelopes and Audio FX, played by a
timeline; the trip-hop edit of examples/demo goes through its own Audio FX under them. The pictures listen to the sound
(Hits → Envelope), and a thread, a white line drawn from the waveform, runs from the first second to the last.
"""
import copy
import json
import math
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]  # the project's folder
W, H = 1920, 1080
BPM = 78
BEAT = 60 / BPM
BAR = 4 * BEAT
S16 = BEAT / 4
SWING = 0.1
TRACK = "../demo/audio/abydos_music-science-of-trip-hop-30-sec-edit-202809.mp3"
INTRO, DROP, BREAK, DROP2, OUTRO, END = 0, 4, 8, 10, 18, 20  # bars
TOTAL = END * BAR + 6
rng = random.Random(1978)
layers, tracks = [], {}
_id = [100]


def new_id():
    _id[0] += 1
    return str(_id[0])


def t16(bar, s):
    return bar * BAR + (s + (SWING if int(s) & 1 else 0)) * S16


def key(layer, param, t, value, curve="hold"):
    tracks.setdefault((layer, param), []).append([round(max(0.0, t), 4), value] + ([curve] if curve is not None else []))


# ---------------------------------------------------------------------------------------------------------------------
# The sound: each voice a layer, its instrument, its envelope, its Audio FX
# ---------------------------------------------------------------------------------------------------------------------
(HERE / "audio").mkdir(exist_ok=True)


def voice(name, kind, freq=110.0, ratio=1.0, index=0.0, gain=0.3, attack=0.002, decay=0.1, sustain=0.0, release=0.03, gate=0, fx=()):
    recipe = f"audio/{name.lower().replace(' ', '-')}.nyxaudio"
    (HERE / recipe).write_text(json.dumps({"version": 1, "type": kind, "frequency": freq, "ratio": ratio, "index": index, "gain": gain, "attack": attack, "decay": decay,
                                           "sustain": sustain, "release": release, "gate": gate}) + "\n")
    lid = new_id()
    effects = []
    for fx_kind, values, wet in fx:
        effects.append({"id": f"{lid}-{fx_kind}", "kind": fx_kind, "enabled": True, "wet": wet, "values": values})
    layers.append({"id": lid, "name": name, "enable": True, "locked": False, "viewports": {}, "fx": [], "play": True, "source": {"type": "audio", "path": recipe, "relative_path": recipe},
                   "audio_fx": effects, "params": {"play": True, "play_mode": "loop", "volume": 1, "audio_fx/enable": True}})
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


G1, F1, Bb1, D2, G2, C2, Eb2, F2, Bb2, D3, G3 = 49.0, 43.65, 58.27, 73.42, 98.0, 65.41, 77.78, 87.31, 116.54, 146.83, 196.0
# The rhythms
KICK = voice("Kick", "fm", 47, 1, 0, 0.34, decay=0.26, fx=[drive(0.2, 5000)])
KNOCK = voice("Kick Knock", "fm", 180, 2.02, 1.5, 0.12, decay=0.05)
TICK = voice("Kick Click", "white", gain=0.1, decay=0.012, fx=[hp(2500)])
SNARE = voice("Snare", "pink", gain=0.42, decay=0.17, fx=[bp(1900, 0.7), room(0.5, 0.18)])
BODY = voice("Snare Body", "fm", 190, 1, 0.3, 0.16, decay=0.08)
HAT = voice("Hat", "white", gain=0.2, decay=0.03, fx=[hp(7500, 0.9)])
OPEN = voice("Open Hat", "white", gain=0.18, decay=0.2, fx=[hp(6500, 0.9), room(0.4, 0.12)])
TOM = voice("Tom", "fm", 90, 1.5, 0.7, 0.16, decay=0.16, fx=[echo(BEAT * 750, 0.3, 0.2)])
RIM = voice("Rim", "fm", 1250, 2.41, 3.0, 0.11, decay=0.022, fx=[echo(BEAT * 375, 0.5, 0.35), room(0.7, 0.25)])
SUB = voice("Sub", "fm", G1, 1, 0.5, 0.11, attack=0.004, decay=0.2, sustain=0.7, release=0.06, fx=[drive(0.35, 1800), lp(900)])
# The drones: long notes that beat against each other, slowly opening and closing
D_LOW = voice("Drone Low", "fm", G1, 1.0, 0.4, 0.035, attack=2.5, decay=0.1, sustain=1, release=3, gate=1, fx=[lp(320, 1.1), room(0.9, 0.2)])
D_FIFTH = voice("Drone Fifth", "fm", D2 * 1.003, 1.5, 0.9, 0.05, attack=3.5, decay=0.1, sustain=1, release=3, gate=1, fx=[lp(600, 1.2), room(0.92, 0.4)])
D_OCT = voice("Drone Octave", "fm", G2 * 0.998, 2.01, 1.6, 0.05, attack=5, decay=0.1, sustain=1, release=3, gate=1, fx=[lp(800, 1.5), echo(BEAT * 1500, 0.55, 0.3), room(0.95, 0.5)])
D_HIGH = voice("Drone High", "fm", G3 * 1.002, 3.003, 2.4, 0.03, attack=6, decay=0.1, sustain=1, release=4, gate=1, fx=[bp(1800, 1.5), room(0.96, 0.7)])
D_AIR = voice("Drone Air", "pink", gain=0.1, attack=4, decay=0.1, sustain=1, release=4, gate=1, fx=[bp(700, 2.2), room(0.95, 0.6)])
RISE = voice("Riser", "white", gain=0.32, attack=0.01, sustain=1, release=0.06, fx=[bp(400, 1.5), room(0.7, 0.3)])
BOOM = voice("Boom", "fm", 120, 1, 0, 0.16, decay=1.8, fx=[room(0.9, 0.3)])
# The edit of the demo, under them, through its effects
TRACK_FX = [("highpass", {"cutoff": 20, "resonance": 0.7}, 1), ("lowpass", {"cutoff": 300, "resonance": 1}, 1), ("delay", {"time_ms": BEAT * 750, "feedback": 0.5}, 0), ("reverb", {"room": 0.85, "damping": 0.4}, 0)]


def track_layer(name):
    lid = new_id()
    layers.append({"id": lid, "name": name, "enable": True, "locked": False, "viewports": {}, "play": False, "fx": [], "source": {"type": "audio", "path": TRACK, "relative_path": TRACK},
                   "audio_fx": [{"id": f"{lid}-{k}", "kind": k, "enabled": True, "wet": w, "values": v} for k, v, w in TRACK_FX], "params": {"play": False, "play_mode": "oneshot", "volume": 0.5}})
    return lid


TAPE_A, TAPE_B = track_layer("Tape A"), track_layer("Tape B")
SOUND_LAYERS = layers[:]
layers.clear()


def hit(layer, t, vel=1.0, gate_len=0.03):
    key(layer, "synth/gate", t, round(vel, 3))
    key(layer, "synth/gate", t + gate_len, 0)


def kick(t, vel=1.0):
    for lid in (KICK, KNOCK, TICK):
        hit(lid, t, vel)


def snare(t, vel=1.0):
    hit(SNARE, t, vel)
    hit(BODY, t, vel)


def sub(t, length, freq, vel=1.0):
    key(SUB, "synth/frequency", t - 0.001, freq)
    key(SUB, "synth/gate", t, vel)
    key(SUB, "synth/gate", t + length, 0)


for layer in (KICK, KNOCK, TICK, SNARE, BODY, HAT, OPEN, TOM, RIM, SUB, RISE, BOOM):
    key(layer, "synth/gate", 0, 0)

ROOTS = [G1, G1, F1, G1, Bb1, G1, F1, G1]
for bar in range(END):
    root = ROOTS[bar % 8]
    # Toms and rims go in threes and fives against the four of the kick: rhythms that meet again every so often
    if DROP <= bar < BREAK or DROP2 <= bar < OUTRO:
        big = bar >= DROP2
        for s in (0, 7, 10) if bar & 1 == 0 else (0, 3, 6, 10) + ((14,) if big else ()):
            kick(t16(bar, s), 1 if s == 0 else 0.85)
            sub(t16(bar, s) + 0.002, S16 * (5 if s == 0 else 2.4), root * (2 if big and s == 10 else 1))
        snare(t16(bar, 4))
        snare(t16(bar, 12))
        if big:
            snare(t16(bar, 15), 0.4)
        for s in range(0, 16, 1 if big else 2):
            hit(HAT, t16(bar, s), 1 if s % 4 == 0 else 0.6 if s % 2 == 0 else 0.35, 0.015)
        if bar & 1:
            hit(OPEN, t16(bar, 14), 0.9, 0.06)
        for k in range(0, 16, 3):
            hit(TOM, t16(bar, k), 0.5 + 0.1 * ((k // 3) % 4), 0.02)
            key(TOM, "synth/frequency", t16(bar, k) - 0.002, [82, 98, 110, 147, 196][(k // 3) % 5] * (1 if big else 0.75))
        for k in range(0, 16, 5):
            hit(RIM, t16(bar, min(k + 2, 15)), 0.8, 0.012)
    # Non-linear: bursts that speed up and slow down, in the intro and the break, on drones alone
    if bar < DROP or BREAK <= bar < DROP2 or bar >= OUTRO:
        for start, count, first, ratio in ((0.0, 7, 0.34, 0.74), (2.2, 9, 0.05, 1.28)):
            at = bar * BAR + start * BEAT
            gap = first
            for n in range(count):
                if at >= (bar + 1) * BAR:
                    break
                hit(RIM, at, 0.3 + 0.7 * n / count, 0.012)
                if bar >= 2:
                    hit(TOM, at, 0.25 + 0.6 * n / count, 0.02)
                    key(TOM, "synth/frequency", at - 0.002, 70 + 26 * (n % 5))
                at += gap
                gap *= ratio
        if 2 <= bar < DROP or BREAK <= bar < DROP2:
            kick(t16(bar, 0), 0.8)
            if bar & 1:
                kick(t16(bar, 11), 0.55)
    if OUTRO <= bar:
        kick(t16(bar, 0), 1)
        sub(t16(bar, 0) + 0.002, BEAT * 2, G1)
for s in range(16):
    snare(t16(17, s), 0.25 + s * 0.05)
    snare(t16(9, s), 0.1 + s * 0.05)
# The hits
for bar in (DROP, DROP2, END):
    hit(BOOM, bar * BAR, 1, 0.05)
    key(BOOM, "synth/frequency", bar * BAR - 0.002, 150)
    key(BOOM, "synth/frequency", bar * BAR + 0.5, 34, 1)
kick(END * BAR, 1)
sub(END * BAR, 3.4, G1)
# The risers
key(RISE, "volume", 0, 0)
for a, b in ((3, 4), (9, 10), (17, 18)):
    key(RISE, "synth/gate", a * BAR, 1)
    key(RISE, "synth/gate", b * BAR - 0.02, 0)
    key(RISE, f"audio_fx/{RISE}-bandpass/cutoff", a * BAR, 300, 0)
    key(RISE, f"audio_fx/{RISE}-bandpass/cutoff", b * BAR, 9000, 0)
    key(RISE, "volume", a * BAR, 0, 0)
    key(RISE, "volume", b * BAR - 0.01, 1, 0)
    key(RISE, "volume", b * BAR, 0)
# The drones move: a chord under the bars, their filters opening
for bar, (low, fifth, oct3) in {0: (G1, D2, G2), 4: (G1, D2, G2), 6: (F1, C2, F2), 8: (Eb2 / 2, Bb1, G2), 10: (G1, D2, G2), 12: (F1, C2, F2), 14: (Bb1, F2, Bb2), 16: (G1, D2, G2)}.items():
    t = bar * BAR
    for lid, f, drift in ((D_LOW, low, 1.0), (D_FIFTH, fifth, 1.003), (D_OCT, oct3, 0.998)):
        key(lid, "synth/frequency", t, round(f * drift, 3))
for lid, fx_kind, curve in ((D_LOW, "lowpass", [(0, 200), (4, 500), (8, 300), (10, 900), (18, 1400), (20, 120)]), (D_FIFTH, "lowpass", [(0, 200), (4, 900), (8, 2400), (10, 700), (16, 2600), (20, 200)]),
                            (D_OCT, "lowpass", [(0, 300), (4, 900), (8, 3500), (10, 600), (17, 3500), (21, 200)]), (D_HIGH, "bandpass", [(0, 700), (8, 2600), (10, 900), (18, 4200), (21, 500)]),
                            (D_AIR, "bandpass", [(0, 300), (4, 1400), (9, 5200), (10, 500), (18, 3000), (21, 300)])):
    for bar, c in curve:
        key(lid, f"audio_fx/{lid}-{fx_kind}/cutoff", bar * BAR, c, 0)
for lid, curve in ((D_LOW, [(0, 1), (21.5, 1), (22, 0)]), (D_FIFTH, [(0, 0.8), (8, 1.3), (10, 0.7), (20, 1), (22, 0)]), (D_OCT, [(0, 0.5), (8, 1.4), (10, 0.5), (18, 1.2), (22, 0)]), (D_HIGH, [(0, 0.2), (8, 1.5), (10, 0.5), (18, 1.3), (22, 0)]),
                   (D_AIR, [(0, 0.5), (8, 1.2), (10, 0.3), (18, 1), (22, 0)])):
    for bar, v in curve:
        key(lid, "volume", bar * BAR, v, 0)
# The tape: filtered open, chopped in the break, closed at the end
LOW, HIGH, DELAY, ROOM = (f"audio_fx/{TAPE_A}-lowpass/cutoff", f"audio_fx/{TAPE_A}-highpass/cutoff", f"audio_fx/{TAPE_A}-delay/wet", f"audio_fx/{TAPE_A}-reverb/wet")
key(TAPE_A, LOW, 0, 220, 0)
key(TAPE_A, LOW, 4 * BAR, 14000, 0)
key(TAPE_A, HIGH, 0, 20)
key(TAPE_A, HIGH, 4 * BAR, 110, 0)
key(TAPE_A, HIGH, 8 * BAR, 110, 0)
key(TAPE_A, HIGH, 10 * BAR, 1400, 0)
key(TAPE_A, DELAY, 0, 0)
key(TAPE_A, DELAY, 8 * BAR, 0.6)
key(TAPE_A, ROOM, 0, 0)
key(TAPE_A, ROOM, 8 * BAR, 0.5)
key(TAPE_A, "volume", 0, 0.4)
key(TAPE_A, "volume", 4 * BAR, 0.34)
for s in range(32):  # Chopped in time
    key(TAPE_A, "volume", 8 * BAR + s * S16, 0.42 if (s % 3) != 2 else 0.05)
LOW_B, ROOM_B = (f"audio_fx/{TAPE_B}-lowpass/cutoff", f"audio_fx/{TAPE_B}-reverb/wet")
key(TAPE_B, f"audio_fx/{TAPE_B}-highpass/cutoff", 0, 120)
key(TAPE_B, LOW_B, 0, 16000)
key(TAPE_B, LOW_B, OUTRO * BAR, 16000, 0)
key(TAPE_B, LOW_B, END * BAR, 260, 0)
key(TAPE_B, ROOM_B, 0, 0)
key(TAPE_B, ROOM_B, OUTRO * BAR, 0.5)
key(TAPE_B, "volume", 0, 0.3)
key(TAPE_B, "volume", END * BAR, 0.3, 0)
key(TAPE_B, "volume", END * BAR + 1, 0, 0)


# ---------------------------------------------------------------------------------------------------------------------
# The pictures
# ---------------------------------------------------------------------------------------------------------------------
def shader(name):
    return f"shaders/{name}.fs"


def generator(name, sh, params=None, blend="normal", opacity=1.0, size=(W, H), fx=()):
    lid = new_id()
    p = {"opacity": opacity, "blend_mode": blend}
    p.update({(k if k.startswith("param/") else f"param/{k}"): v for k, v in (params or {}).items()})
    layers.append({"id": lid, "name": name, "enable": True, "locked": False, "viewports": {}, "fx": [{"path": shader(f), "relative_path": shader(f)} for f in fx],
                   "source": {"type": "isf", "path": shader(sh), "relative_path": shader(sh), "width": size[0], "height": size[1]}, "params": p})
    return lid


# Top first: the thread, the flash, the world
FIL = generator("The Thread", "Fil", {"x": 0.5, "reach": 0.0, "amp": 0.05, "width": 0.0009, "glow": 0.2, "count": 1, "spread": 0.018}, "add")
FLASH = generator("Flash", "SolidColor", {"color/r": 1, "color/g": 1, "color/b": 1, "color/a": 1}, "add", 0)

WORLD = new_id()
FX_NAMES = ["Kaleidoscope", "Zoom Volumetric", "Pixelate", "VHS Glitch", "FlipFlop", "Trails"]
SEG = {n: n.replace(" ", "_") for n in FX_NAMES}
layers.append({"id": WORLD, "name": "World", "enable": True, "locked": False, "viewports": {}, "group": True, "collapsed": True, "source": {"type": "none"},
               "fx": [{"path": shader(n), "relative_path": shader(n)} for n in FX_NAMES],
               "params": {"opacity": 1, "blend_mode": "normal", "scale/x": 100, "scale/y": 100, "position/x": W / 2, "position/y": H / 2, "pivot/x": 50, "pivot/y": 50, "fx/enable": True,
                          **{f"fx/{SEG[n]}/enable": False for n in FX_NAMES}}})
SOURCES = ["Sea", "Stage", "Lattice", "Bars", "Data", "Ridges", "Spectrum", "Waves"]
WINDOWS = []
WIN_SOURCES = {}
for k in range(14):
    WINDOWS.append(new_id())
    layers.append({"id": WINDOWS[-1], "name": f"Window {k + 1}", "parent": WORLD, "enable": True, "locked": False, "viewports": {}, "fx": [], "source": {"type": "layer", "layer": "0", "tap": "postfx"},
                   "params": {"opacity": 0, "blend_mode": "normal", "position/x": W / 2, "position/y": H / 2, "scale/x": 100, "scale/y": 100, "rotation": 0, "roi/left": 0, "roi/top": 0, "roi/right": 1, "roi/bottom": 1}})
black = new_id()
layers.append({"id": black, "name": "Black", "parent": WORLD, "enable": True, "locked": False, "viewports": {}, "fx": [],
               "source": {"type": "isf", "path": shader("SolidColor"), "relative_path": shader("SolidColor"), "width": 64, "height": 64},
               "params": {"opacity": 1, "blend_mode": "normal", "param/color/r": 0, "param/color/g": 0, "param/color/b": 0, "param/color/a": 1}})

# The sources, under the world: what its windows show
SEA = generator("Sea", "Sea", {"scale": 3.2, "speed": 0.1, "wake": 0.6, "slab": 1, "spark": 0.6})
LATTICE = generator("Lattice", "Beam Lattice", {"speed": 0.5, "cell": 2, "thickness": 0.012, "glow": 0.8, "fog": 0.6, "warp": 0.2, "fov": 80})
BARS = generator("Bars", "Light Bars", {"count": 12, "width": 0.1, "speed": 0.3, "mirror": True})
DATA = generator("Data", "Data Stream", {"rows": 40, "density": 0.5, "speed": 0.6, "flicker": 0.3})
RIDGES = generator("Ridges", "Ridgelines", {"scale": 4, "speed": 0.3, "sharpness": 3, "gain": 2.2, "contrast": 1.6, "colorLow/r": 0.01, "colorLow/g": 0.05, "colorLow/b": 0.07, "colorHigh/r": 0.8, "colorHigh/g": 1, "colorHigh/b": 1})
SPECTRUM = generator("Spectrum", "FFT Spectrogram", {"gain": 1.4, "range": 0.7, "lumaMode": 1})
WAVES = generator("Waves", "Waveform Lines", {"lines": 44, "amplitude": 0.8, "depth": 0.7, "tilt": 0.3, "thickness": 1.2})

# The 3D stage: the monolith, standing in the sea on the thread's axis
STAGE = new_id()
layers.append({"id": STAGE, "name": "Stage", "enable": True, "locked": False, "viewports": {}, "group": True, "scene3d": True, "collapsed": True, "source": {"type": "none"}, "fx": [],
               "params": {"opacity": 1, "blend_mode": "normal", "camera/distance": 7, "camera/fov": 45, "camera/pitch": 12, "camera/yaw": 0, "camera/target/x": 0, "camera/target/y": 1.2, "camera/target/z": 0,
                          "scene/background/r": 0, "scene/background/g": 0.01, "scene/background/b": 0.015, "scene/background/a": 1, "scene/studio": 0.5, "scene/ambient_intensity": 0.05, "scene/exposure": 1.2,
                          "scene/glow": 0.9, "scene/glow_threshold": 0.8, "scene/glow_size": 0.7, "scene/contrast": 1.1, "scene/vignette": 0.35, "scene/fog/density": 0.11, "scene/fog/base": -1, "scene/fog/falloff": 0.2, "scene/fog/reach": 30,
                          "scene/fog/color/r": 0, "scene/fog/color/g": 0.004, "scene/fog/color/b": 0.006}})
# Added after the 3D objects, so that they stand right under their scene
_stage_at = len(layers)


def model(name, shape, parent=STAGE, position=(0, 0, 0), rotation=(0, 0, 0), scale=1.0, axes=(1, 1, 1), color=(0.8, 0.8, 0.8), metal=0.0, rough=0.5, detail=1, displace=None, amount=0.2, picture=None, unlit=False, smooth=False):
    src = {"type": "model", "path": f"/NyxMap/3D/{shape}"}
    if displace:
        src.update(displace=displace, displace_stage=-1)
    if picture:
        src.update(picture=picture, picture_stage=-1)
    p = {"model/fit": True, "model/detail": detail, "model/unlit": unlit, "model/scale/all": scale, "model/material/metal": metal, "model/material/rough": rough, "model/displace/amount": amount, "model/displace/mid": 0.5}
    if smooth:
        p["model/shading"] = "smooth"
    for i, a in enumerate("xyz"):
        p[f"model/position/{a}"], p[f"model/rotation/{a}"], p[f"model/scale/{a}"] = position[i], rotation[i], axes[i]
    for i, a in enumerate("rgb"):
        p[f"model/material/color/{a}"] = color[i]
    lid = new_id()
    layers.append({"id": lid, "name": name, "enable": True, "locked": False, "viewports": {}, "parent": parent, "source": src, "fx": [], "params": p})
    return lid


def merge(name, position=(0, 0, 0)):
    lid = new_id()
    layers.append({"id": lid, "name": name, "enable": True, "locked": False, "viewports": {}, "group": True, "merge3d": True, "collapsed": True, "parent": STAGE, "source": {"type": "none"}, "fx": [],
                   "params": {"merge/position/x": position[0], "merge/position/y": position[1], "merge/position/z": position[2], "merge/scale/all": 1}})
    return lid


def light(name, kind, position, color, intensity, look=None, path=None, angle=25.0, falloff=0.4, soft=3.0, rays=1.0, size=(4, 2)):
    p = {"light/type": kind, "light/intensity": intensity, "light/level": 1, "light/angle": angle, "light/falloff": falloff, "light/softness": soft, "light/rays": rays, "light/shadow": True, "light/width": size[0], "light/height": size[1],
         "light/pitch": -89.5, "light/yaw": 0}
    for i, a in enumerate("xyz"):
        p[f"light/position/{a}"] = position[i]
    for i, a in enumerate("rgb"):
        p[f"light/color/{a}"] = color[i]
    if look:
        p["light/lookat"] = True
        for i, a in enumerate("xyz"):
            p[f"light/lookpoint/{a}"] = look[i]
    if path:
        p["light/path/shape"], p["light/path/width"], p["light/path/depth"], p["light/path/speed"] = path
    lid = new_id()
    layers.append({"id": lid, "name": name, "enable": True, "locked": False, "viewports": {}, "parent": STAGE, "source": {"type": "light"}, "fx": [], "params": p})
    return lid


NOISE = new_id()
MONO = model("Monolith", "Cube", position=(0, 1.5, 0), axes=(0.34, 2.4, 0.12), scale=1.5, color=(0.02, 0.02, 0.025), metal=1, rough=0.12, detail=6, displace=NOISE, amount=0.02)
OCEAN = model("Sea Plane", "Plane", position=(0, -1, 0), rotation=(-90, 0, 0), scale=30, color=(1, 1, 1), metal=0.2, rough=0.25, detail=8, displace=NOISE, amount=0.05, picture=SEA)
HALO = merge("Halo", (0, 1.5, 0))
FOAM = model("Noise Ring", "Torus", HALO, scale=1.8, color=(0.9, 0.95, 1), metal=0.9, rough=0.2, detail=7, displace=NOISE, amount=0.14, rotation=(90, 0, 0), smooth=True)
RING2 = model("Noise Ring 2", "Torus", HALO, scale=2.6, color=(0.1, 0.5, 0.55), metal=0.8, rough=0.25, detail=7, displace=NOISE, amount=0.2, rotation=(70, 20, 0), smooth=True)
ROCKS = merge("Stones", (0, 0.4, 0))
STONES = [model(f"Stone {k + 1}", "Sphere", ROCKS, position=(3.2 * math.cos(k * 1.256), 0.2 * math.sin(k * 2.1), 3.2 * math.sin(k * 1.256)), scale=0.45, color=(0.55, 0.6, 0.62), metal=0.7, rough=0.35, detail=7, displace=NOISE, amount=0.5) for k in range(5)]
RAYS = merge("White Lines", (0, 1.5, 0))
for k in range(14):
    a = k * 360 / 14
    model(f"Line {k + 1}", "Cube", RAYS, position=(4.4 * math.cos(math.radians(a)), 0, 4.4 * math.sin(math.radians(a))), rotation=(0, -a, 0), axes=(3, 0.01, 0.01), color=(1, 1, 1), unlit=True)
DOTS = merge("White Points", (0, 1.5, 0))
for k in range(30):
    y = 1 - 2 * (k + 0.5) / 30
    q, th = math.sqrt(1 - y * y), k * math.pi * (3 - math.sqrt(5))
    model(f"Point {k + 1}", "Sphere", DOTS, position=(3.4 * q * math.cos(th), 3.4 * y, 3.4 * q * math.sin(th)), scale=0.03, color=(1, 1, 1), unlit=True)
SPOT = light("Beam", "spot", (0, 7, 0), (0.85, 1, 1), 160, look=(0, 0, 0), path=(1, 6, 6, 0.08), angle=11, falloff=0.3, rays=1.6)
SPOT2 = light("Beam 2", "spot", (-5, 6, 3), (1, 0.55, 0.3), 90, look=(0, 2, 0), path=(2, 8, 5, -0.13), angle=9, falloff=0.3, rays=1.4)
PANEL = light("Panel", "area", (0, 8, 0), (0.75, 0.95, 1), 26, size=(8, 3), rays=0)
_id_noise = NOISE
layers.append({"id": NOISE, "name": "Noise", "enable": True, "locked": False, "viewports": {}, "fx": [], "source": {"type": "isf", "path": shader("Simplex Noise"), "relative_path": shader("Simplex Noise"), "width": 1024, "height": 512},
               "params": {"opacity": 1, "blend_mode": "normal", "param/scale": 3.4, "param/octaves": 3, "param/speed": 0.8, "param/contrast": 1.4, "param/seed": 5}})
ALL_SOURCES = {"Sea": SEA, "Stage": STAGE, "Lattice": LATTICE, "Bars": BARS, "Data": DATA, "Ridges": RIDGES, "Spectrum": SPECTRUM, "Waves": WAVES}
for k, lid in enumerate(WINDOWS):
    name = SOURCES[k % len(SOURCES)]
    WIN_SOURCES[lid] = name
    for layer in layers:
        if layer["id"] == lid:
            layer["source"]["layer"] = ALL_SOURCES[name]
            layer["name"] = f"Window {k + 1} · {name}"


# ---------------------------------------------------------------------------------------------------------------------
# Every beat, a new image: layouts of windows, crops, scales in all directions
# ---------------------------------------------------------------------------------------------------------------------
def by_source(name, n=0):
    found = [w for w in WINDOWS if WIN_SOURCES[w] == name]
    return found[n % len(found)]


def place(win, x, y, w, h, roi=(0, 0, 1, 1), rot=0.0):
    return (win, x, y, w, h, roi, rot)


def rroi(zoom=None):
    z = zoom or rng.choice((1, 0.7, 0.5, 0.33, 0.2, 0.1))
    x, y = rng.uniform(0, 1 - z), rng.uniform(0, 1 - z)
    return (x, y, x + z, y + z)


def choose_windows(n, must=()):
    rest = [w for w in WINDOWS if w not in must]
    rng.shuffle(rest)
    return list(must) + rest[:max(0, n - len(must))]


def full(name):
    return [place(by_source(name), W / 2, H / 2, W, H)]


def closeup(name):
    return [place(by_source(name), W / 2, H / 2, W, H, rroi(rng.choice((0.33, 0.2, 0.12))))]


def columns(n=3):
    wins = choose_windows(n)
    width = W / n
    return [place(w, width * (i + 0.5), H / 2, width, H, (i / n, 0, (i + 1) / n, 1)) for i, w in enumerate(wins)]


def rows(n=4):
    wins = choose_windows(n)
    height = H / n
    return [place(w, W / 2, height * (i + 0.5), W, height, (0, i / n, 1, (i + 1) / n)) for i, w in enumerate(wins)]


def grid(n=3):
    wins = choose_windows(min(n * n, len(WINDOWS)))
    w_, h_ = W / n, H / n
    return [place(w, w_ * ((i % n) + 0.5), h_ * ((i // n) + 0.5), w_ * 0.97, h_ * 0.97, rroi(0.5)) for i, w in enumerate(wins)]


def slits(n=7):
    """Long thin cuts on each side of the thread."""
    wins = choose_windows(n)
    out = []
    for i, w in enumerate(wins):
        s = (i - (n - 1) / 2) / n
        width = W * rng.uniform(0.012, 0.09)
        out.append(place(w, W / 2 + s * W * 0.95, H / 2, width, H, (0.5 + s * 0.8 - width / W / 2, 0, 0.5 + s * 0.8 + width / W / 2, 1)))
    return out


def inset(name):
    base = place(by_source(name), W / 2, H / 2, W, H)
    out = [base]
    for w in choose_windows(4):
        if w == base[0]:
            continue
        size = rng.uniform(0.12, 0.34)
        out.append(place(w, rng.uniform(0.15, 0.85) * W, rng.uniform(0.15, 0.85) * H, W * size, H * size, rroi(0.5)))
    return out


def pagaille(n=9):
    """Scales in all directions."""
    out = []
    for w in choose_windows(n):
        sx, sy = rng.choice((0.04, 0.08, 0.2, 0.4, 0.7, 1.3, 2.4)), rng.choice((0.03, 0.06, 0.15, 0.3, 0.6, 1.2, 2))
        out.append(place(w, rng.uniform(0, 1) * W, rng.uniform(0, 1) * H, W * sx, H * sy, rroi(), rng.choice((0, 0, 0, 90, -45, 12, -7, 30))))
    return out


def tumble(name):
    """One image, again and again, smaller and turned, to the middle."""
    wins = [w for w in WINDOWS if WIN_SOURCES[w] == name] + choose_windows(5)
    out = []
    s = 1.0
    for i, w in enumerate(wins[:6]):
        out.append(place(w, W / 2, H / 2, W * s, H * s, (0, 0, 1, 1), i * 11.0))
        s *= 0.68
    return out


def steps_of(bar):
    if bar < 2:
        return 1
    if bar < DROP:
        return 2
    if bar < BREAK:
        return 4
    if bar < DROP2:
        return 8 if bar == BREAK + 1 else 4
    if bar < OUTRO:
        return 8 if (bar - DROP2) % 4 == 3 else 4
    return 2 if bar == OUTRO else 1


SEQ_INTRO = [lambda: full("Sea"), lambda: closeup("Sea"), lambda: full("Stage"), lambda: slits(5), lambda: inset("Sea"), lambda: full("Waves"), lambda: columns(3)]
SEQ_DROP = [lambda: full("Stage"), lambda: columns(3), lambda: closeup("Stage"), lambda: rows(5), lambda: full("Lattice"), lambda: inset("Stage"), lambda: pagaille(7), lambda: full("Sea"), lambda: slits(9), lambda: tumble("Stage"),
            lambda: full("Bars"), lambda: closeup("Ridges"), lambda: grid(3), lambda: full("Data")]
SEQ_BREAK = [lambda: slits(11), lambda: full("Spectrum"), lambda: tumble("Sea"), lambda: closeup("Waves"), lambda: pagaille(12), lambda: full("Ridges"), lambda: grid(4), lambda: rows(9)]
SEQ_BIG = SEQ_DROP + [lambda: pagaille(12), lambda: pagaille(14), lambda: grid(4), lambda: tumble("Lattice"), lambda: rows(9), lambda: columns(7), lambda: closeup("Stage"), lambda: inset("Lattice"), lambda: tumble("Ridges")]
SEQ_OUT = [lambda: full("Stage"), lambda: slits(5), lambda: full("Sea"), lambda: closeup("Stage")]

CAMERAS = [(7, 8, -20, 1.2, 45), (3.2, 4, 40, 2.4, 60), (10, 42, 0, 0.5, 40), (5, -6, 100, 2.6, 70), (14, 80, 10, 0, 35), (2.6, 18, 200, 0.4, 85), (8, 25, 270, 1.5, 28)]
for win in WINDOWS:
    key(win, "opacity", 0, 0)
last_on = set()
seen = 0
for bar in range(END + 2):
    n = steps_of(bar)
    seq = SEQ_INTRO if bar < DROP else SEQ_DROP if bar < BREAK else SEQ_BREAK if bar < DROP2 else SEQ_BIG if bar < OUTRO else SEQ_OUT
    for s in range(n):
        at = (bar + s / n) * BAR
        layout = seq[seen % len(seq)]()
        seen += 1
        now = set()
        for win, x, y, w, h, roi, rot in layout:
            if win in now:
                continue
            now.add(win)
            key(win, "opacity", at, 1)
            key(win, "position/x", at, round(x, 1))
            key(win, "position/y", at, round(y, 1))
            key(win, "scale/x", at, round(w / W * 100, 2))
            key(win, "scale/y", at, round(h / H * 100, 2))
            key(win, "rotation", at, rot)
            for side, v in zip(("left", "top", "right", "bottom"), roi):
                key(win, f"roi/{side}", at, round(min(max(v, 0), 1), 4))
        for win in last_on - now:
            key(win, "opacity", at, 0)
        last_on = now
        # What the world goes through, at this step: more and more of it
        wild = 0.12 if bar < DROP else 0.3 if bar < BREAK else 0.55 if bar < DROP2 else 0.5 if bar < OUTRO else 0.1
        for fx_name in FX_NAMES:
            key(WORLD, f"fx/{SEG[fx_name]}/enable", at, rng.random() < wild * (0.35 if fx_name == "Trails" else 0.4))
        key(WORLD, "fx/Kaleidoscope/param/segments", at, rng.choice((3, 4, 6, 8, 12)))
        key(WORLD, "fx/Kaleidoscope/param/zoom", at, rng.choice((0.6, 1, 1.5, 2.4)))
        key(WORLD, "fx/Pixelate/param/cellSize", at, rng.choice((6, 12, 24, 60, 120)))
        key(WORLD, "fx/Pixelate/param/gap", at, rng.choice((0, 0.1, 0.3)))
        key(WORLD, "fx/FlipFlop/param/flip", at, rng.random() < 0.5)
        key(WORLD, "fx/FlipFlop/param/flop", at, rng.random() < 0.5)
        key(WORLD, "fx/Trails/param/decay", at, round(rng.uniform(0.7, 0.96), 3))
        key(BARS, "param/angle", at, rng.choice((0, 90, 45, -45, 22)))
        key(BARS, "param/count", at, rng.choice((3, 8, 16, 40)))
        key(LATTICE, "param/cell", at, rng.choice((1, 2, 3, 4)))
        key(DATA, "param/mode", at, rng.randrange(0, 3))
        key(RIDGES, "param/scale", at, rng.choice((2, 4, 8, 14)))
        key(SEA, "param/warm", at, 1 if rng.random() < (0.05 if bar < DROP2 else 0.18) else 0)
    # The camera, cut at each bar
    cam = CAMERAS[(bar * 3) % len(CAMERAS)]
    if bar < END:
        key(STAGE, "camera/distance", bar * BAR, cam[0] * (1.5 if bar < DROP else 1))
        key(STAGE, "camera/pitch", bar * BAR, cam[1])
        key(STAGE, "camera/yaw", bar * BAR, cam[2], 0)
        key(STAGE, "camera/yaw", (bar + 1) * BAR - 0.02, cam[2] + 28)
        key(STAGE, "camera/target/y", bar * BAR, cam[3])
        key(STAGE, "camera/fov", bar * BAR, cam[4])
# The thread, all along: it appears, lengthens, splits in the break, and is one again at the end
THREAD = {"param/reach": [(0, 0, 0), (2, 0.25, 0), (4, 1.0, 0), (20, 1.0, 0), (21, 0.12, 0), (22, 0, 0)],
          "param/width": [(0, 0.0006, 0), (4, 0.0009, 0), (8, 0.0009, 0), (10, 0.0007, "hold"), (10.01, 0.0016, 0), (18, 0.0022, 0), (20, 0.0009, 0)],
          "param/count": [(0, 1, 0), (8, 1, 0), (9.8, 40, "hold"), (10, 1, "hold"), (14, 3, "hold"), (17, 3, 0), (18, 9, 0), (19.9, 1, 0)],
          "param/spread": [(0, 0.012, 0), (8, 0.012, 0), (9.8, 0.024, "hold"), (10, 0.012, 0), (18, 0.03, 0), (20, 0.004, 0)]}
for param, points in THREAD.items():
    for bar, v, curve in points:
        key(FIL, param, bar * BAR, v, curve)
key(STAGE, "scene/fog/density", 0, 0.2, 0)
key(STAGE, "scene/fog/density", 8 * BAR, 0.1, 0)
key(STAGE, "scene/fog/density", 10 * BAR - 0.02, 0.32, "hold")
key(STAGE, "scene/fog/density", 10 * BAR, 0.08, 0)
key(STAGE, "scene/fog/density", 18 * BAR, 0.1, 0)
key(STAGE, "scene/fog/density", 21 * BAR, 0.4, 0)
key(WORLD, "opacity", 0, 1, 0)
key(WORLD, "opacity", END * BAR + 3, 1, 0)
key(WORLD, "opacity", END * BAR + 5.5, 0, 0)
for layer, param, turns in ((HALO, "merge/rotation/x", 720), (HALO, "merge/rotation/y", 1440), (ROCKS, "merge/rotation/y", -1080), (DOTS, "merge/rotation/y", 720), (RAYS, "merge/rotation/y", -360)):
    key(layer, param, 0, 0, 0)
    key(layer, param, TOTAL, turns, 0)

# ---------------------------------------------------------------------------------------------------------------------
# The pictures listen to the sound: its hits, heard in the mix as it plays, become envelopes
# ---------------------------------------------------------------------------------------------------------------------
LOW_HITS, MID_HITS, HIGH_HITS = (35, 120), (1400, 4500), (8000, 16000)
ENVELOPES = [
    (LOW_HITS, WORLD, "scale/x", 100, 109, 0.003, 0.16), (LOW_HITS, WORLD, "scale/y", 100, 109, 0.003, 0.16), (LOW_HITS, FIL, "param/amp", 0.04, 0.3, 0.003, 0.22), (LOW_HITS, FIL, "param/glow", 0.2, 2.6, 0.003, 0.2),
    (LOW_HITS, SEA, "param/hit", 0, 1, 0.003, 0.3), (LOW_HITS, LATTICE, "param/hit", 0, 1, 0.003, 0.25), (LOW_HITS, BARS, "param/hit", 0, 1, 0.003, 0.2), (LOW_HITS, DATA, "param/hit", 0, 1, 0.003, 0.18), (LOW_HITS, WAVES, "param/hit", 0, 1, 0.003, 0.2),
    (LOW_HITS, MONO, "model/scale/all", 1.5, 1.85, 0.003, 0.2), (LOW_HITS, FOAM, "model/displace/amount", 0.14, 0.7, 0.004, 0.3), (LOW_HITS, RING2, "model/scale/all", 2.6, 3.3, 0.004, 0.32), (LOW_HITS, OCEAN, "model/displace/amount", 0.05, 0.16, 0.004, 0.4),
    (LOW_HITS, SPOT, "light/intensity", 160, 700, 0.002, 0.2), (LOW_HITS, STAGE, "scene/glow", 0.9, 3, 0.002, 0.2), (LOW_HITS, NOISE, "param/contrast", 1.4, 2.6, 0.004, 0.2),
    (MID_HITS, SPOT2, "light/intensity", 90, 700, 0.002, 0.18), (MID_HITS, RING2, "model/bend", 0, 160, 0.004, 0.25), (MID_HITS, FOAM, "model/bend", 0, -110, 0.004, 0.2), (MID_HITS, RAYS, "merge/scale/all", 1, 1.6, 0.003, 0.18),
    (MID_HITS, DATA, "param/glitch", 0, 1, 0.002, 0.14), (MID_HITS, WORLD, "rotation", 0, 1.6, 0.002, 0.14), (MID_HITS, FLASH, "opacity", 0, 0.14, 0.001, 0.09),
    (HIGH_HITS, DOTS, "merge/scale/all", 1, 1.18, 0.002, 0.06), (HIGH_HITS, SEA, "param/spark", 0.6, 3, 0.002, 0.08), (HIGH_HITS, BARS, "param/jitter", 0, 0.5, 0.002, 0.07), (HIGH_HITS, FIL, "param/spread", 0.012, 0.03, 0.002, 0.06),
]
interactions = []
for n, ((lo, hi), layer, param, a, b, attack, decay) in enumerate(ENVELOPES, 1):
    interactions.append({"id": str(n), "name": f"{lo}–{hi} Hz → {param}", "enabled": True, "source": "audio", "audio_low": lo, "audio_high": hi, "audio_feature": 3, "audio_input": False, "audio_sensitivity": 0.6, "audio_gap": 0.08,
                         "layer": layer, "param": param, "master": 1, "takeover": "jump", "filters": [{"type": "envelope", "a": attack, "b": decay, "c": 0, "d": 0.02}, {"type": "remap", "a": 0, "b": 1, "c": a, "d": b}]})
# The kick's pitch falls with its gate, as the snare's
PITCH = [(KICK, KICK, "synth/frequency", 47, 180, 0.03), (KICK, KNOCK, "synth/frequency", 170, 460, 0.012), (BODY, BODY, "synth/frequency", 178, 280, 0.02)]
for k, (source, layer, param, lo, hi, decay) in enumerate(PITCH):
    interactions.append({"id": str(100 + k), "name": f"Gate → {param}", "enabled": True, "source": "internal", "source_layer": source, "source_param": "synth/gate", "layer": layer, "param": param, "master": 1, "takeover": "jump",
                         "filters": [{"type": "envelope", "a": 0.001, "b": decay, "c": 0, "d": 0.01}, {"type": "remap", "a": 0, "b": 1, "c": lo, "d": hi}]})

layers.extend(SOUND_LAYERS)
# Two timelines, one after the other: at the turn, the second tape starts
TURN = round(DROP2 * BAR, 4)


def value_at(keys, t):
    """The value of a track at t: what its keys make of it, for the track to go on from there in the next timeline."""
    before = [k for k in keys if k[0] <= t]
    after = [k for k in keys if k[0] > t]
    if not before:
        return keys[0][1]
    a = before[-1]
    if not after or len(after[0]) < 3 or after[0][2] == "hold" or isinstance(a[1], bool):
        return a[1]
    b = after[0]
    return a[1] + (b[1] - a[1]) * (t - a[0]) / (b[0] - a[0])


first_tracks, second_tracks = [], []
for (layer, param), keys in tracks.items():
    keys.sort(key=lambda k: k[0])
    early = [k for k in keys if k[0] < TURN]
    late = [[round(k[0] - TURN, 4)] + k[1:] for k in keys if k[0] >= TURN]
    if early:
        first_tracks.append({"layer": layer, "param": param, "keys": early})
    if late or len(keys) != len(early):
        if not late or late[0][0] > 0:
            late.insert(0, [0, value_at(keys, TURN), "hold"])
        second_tracks.append({"layer": layer, "param": param, "keys": late})
timeline_tracks = first_tracks + second_tracks
timelines = [{"id": "1", "name": "The Piece · first tape", "duration": TURN, "loop": "once", "tracks": first_tracks},
             {"id": "2", "name": "The Piece · second tape", "duration": round(TOTAL - TURN, 3), "loop": "once", "tracks": second_tracks}]
states = []
for layer in layers:
    state = copy.deepcopy(layer)
    state["included"] = True
    if state["id"] == TAPE_A:
        state["params"] = dict(state["params"], restart=True, play=True, play_mode="oneshot")
    states.append(state)
second_tape = copy.deepcopy(next(l for l in layers if l["id"] == TAPE_B))
second_tape["included"] = True
second_tape["params"] = dict(second_tape["params"], restart=True, play=True, play_mode="oneshot")
others = [dict(copy.deepcopy(l), included=False) for l in layers if l["id"] != TAPE_B]
snapshot2 = {"id": "2", "name": "Second tape", "fade": 0, "layers": others + [second_tape], "composition": {"included": False}, "interactions": []}
snapshot = {"id": "1", "name": "Ready", "fade": 0, "layers": states, "composition": {"included": True, "opacity": 1, "volume": 0.7}, "interactions": [{"id": i["id"], "included": True, "enabled": i["enabled"]} for i in interactions]}
steps = [{"text": "GO", "snapshot": "1", "description": "All of it plays from here, as it is made", "continue": "follow"}, {"text": "First tape", "timeline": "1", "action": "play", "continue": "follow"},
         {"text": "Second tape starts", "snapshot": "2", "continue": "follow"}, {"text": "Second tape", "timeline": "2", "action": "play"}]
project = {"app": "NyxMap", "format_version": 1, "composition": {"width": W, "height": H, "fps": 60, "speed": 1}, "audio": {"volume": 0.7}, "layers": layers, "interactions": interactions, "timelines": timelines, "presets": [], "snapshots": [snapshot, snapshot2], "snapshot_tree": ["1", "2"],
           "sequences": [{"id": "1", "name": "Monolith Live", "loop": False, "steps": steps}], "current_sequence": 0, "startup_sequence": 0, "startup_step": 0, "startup_go": "--autoplay" in sys.argv, "ui": {"selected_layer": 0}}
(HERE / "Monolith Live.nyxmap").write_text(json.dumps(project, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
print(f"{len(layers)} layers, {len(interactions)} interactions, {sum(len(t['keys']) for t in timeline_tracks)} keys on {len(timeline_tracks)} tracks, {seen} images, {TOTAL:.1f} s")
