#!/usr/bin/env python3
"""Vertigo Pressure: 48 s of sub at 140 BPM. A drop that spins the floor, a bass that wobbles, a 3D tunnel that flies
past the camera, and the mapping of the pictures bending under it: corner pins, rotations, a camera on a swell.

    python3 examples/vertigo-pressure/source/build_show.py [--autoplay]

Everything that sounds is a Faust instrument of NyxMap, in its audio callback, played by one timeline: no sound file. Everything
that shows is NyxMap's own (its shaders, its 3D scene, its mapping). Each voice's gate drives its own picture, so a pulse
falls with its note. The timeline's keys are written here, as a score; the envelopes are followed by interactions.
"""
import copy
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
W, H = 1920, 1080
BPM = 140
BEAT = 60 / BPM
BAR = 4 * BEAT
S16 = BEAT / 4
SWING = 0.07  # the off 16ths, a little late
BARS_TOTAL = 28
TOTAL = BARS_TOTAL * BAR
# Sections, in bars
INTRO, BUILD, DROP_A, BREAK, DROP_B, END = 0, 4, 8, 16, 18, 26
E1, G1, A1, B1, E2 = 41.2, 49.0, 55.0, 61.74, 82.41
E3, G3, B3, E4, G4, B4, D5, E5 = 164.81, 196.0, 246.94, 329.63, 392.0, 493.88, 587.33, 659.25
DOTTED = BEAT * 750  # a dotted eighth, in ms: the dub's echo
FRAMES, GAP, NEAR = 16, 2.0, 5.0  # the tunnel: its frames, the space between them, the depth where they wrap (behind the camera)
LOOP = FRAMES * GAP  # the tunnel's length

layers, tracks, interactions = [], {}, []
_id = [100]


def new_id():
    _id[0] += 1
    return str(_id[0])


def key(layer, param, t, value, curve="hold"):
    if t > TOTAL:  # past the end of the timeline: nothing to play, and the importer would clamp it onto the last instant
        return
    tracks.setdefault((layer, param), []).append([round(max(0.0, t), 4), value] + ([curve] if curve is not None else []))


def at(bar, step=0.0):
    return bar * BAR + (step + (SWING if int(step) & 1 else 0)) * S16


def sine(layer, param, t0, t1, cycles_per_bar, amp, center=0.0, phase=0.0, step=1 / 24):
    """A swell on a number: dense linear keys of a sine, from t0 to t1, back to its center at the end."""
    rate = cycles_per_bar / BAR
    t = t0
    while t < t1:
        key(layer, param, t, round(center + amp * math.sin(2 * math.pi * rate * (t - t0) + phase), 4), 0)
        t += step
    key(layer, param, t1, center, 0)


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


KICK = voice("Kick", "fm", 52, 1, 0, 0.5, decay=0.3, fx=[drive(0.2, 6000)])
CLICK = voice("Kick Click", "white", gain=0.07, decay=0.006, fx=[hp(3000)])
SNARE = voice("Snare", "pink", gain=0.38, decay=0.22, fx=[bp(1900, 0.7), echo(DOTTED, 0.62, 0), room(0.8, 0.32)])
CRACK = voice("Snare Crack", "white", gain=0.12, decay=0.05, fx=[hp(1800), room(0.7, 0.2)])
HAT = voice("Hat", "white", gain=0.08, decay=0.025, fx=[hp(8000, 0.9)])
OPEN = voice("Open Hat", "white", gain=0.06, decay=0.2, fx=[hp(7000, 0.9), room(0.4, 0.12)])
# The sub: a walk on E, with a second voice a fifth above it, and the wobble: a filter swept by a swell
SUB = voice("Sub", "fm", E1, 1, 0.08, 0.34, attack=0.008, decay=0.2, sustain=1, release=0.07, envelope="adsr")
WOB = voice("Wobble", "fm", E1, 1, 1.8, 0.26, attack=0.004, decay=0.3, sustain=0.9, release=0.05, envelope="adsr", fx=[drive(0.55, 900), lp(350, 0.85), room(0.5, 0.12)])
WOB_HI = voice("Wobble High", "fm", E2, 2, 0.6, 0.1, attack=0.004, decay=0.3, sustain=0.9, release=0.05, envelope="adsr", fx=[drive(0.3, 2000), lp(900, 0.8)])
# The acid lead: an FM bite whose depth is played live, its echo opened by hand
LEAD = voice("Acid Lead", "fm", E4, 2.0, 2.0, 0.12, attack=0.002, decay=0.12, sustain=0.2, release=0.05, envelope="adsr", fx=[bp(1300, 0.9), echo(DOTTED, 0.55, 0.4), room(0.7, 0.25)])
HORN = voice("Horn", "fm", 523.25, 1.003, 5.5, 0.06, attack=0.005, decay=0.05, sustain=0.9, release=0.03, envelope="adsr", fx=[drive(0.4, 3000), bp(1700, 0.7), echo(DOTTED, 0.55, 0.3), room(0.8, 0.3)])
SIREN = voice("Siren", "fm", 470, 1, 0.5, 0.07, attack=0.02, decay=0.1, sustain=1, release=0.2, envelope="adsr", fx=[bp(1000, 0.7), echo(DOTTED, 0.68, 0.55), room(0.9, 0.45)])
RISE = voice("Riser", "white", gain=0.1, attack=0.01, sustain=1, release=0.05, envelope="adsr", fx=[bp(400, 1.6), room(0.75, 0.3)])
AIR = voice("Air", "pink", gain=0.035, attack=2.5, decay=0.1, sustain=1, release=1.5, envelope="adsr", fx=[bp(900, 2.2), room(0.95, 0.6)])
BOOM = voice("Boom", "fm", 110, 1, 0, 0.32, decay=2.2, fx=[room(0.9, 0.22)])
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


def bass(t, steps, freq, wobble=True):
    for lid, f in ((SUB, freq), (WOB, freq), (WOB_HI, freq * 2)):
        key(lid, "synth/frequency", t - 0.002, f)
        key(lid, "synth/gate", t, 1)
        key(lid, "synth/gate", t + steps * S16 - 0.02, 0)


# ---------------------------------------------------------------------------------------------------------------------
# The pictures: NyxMap's own shaders, the mapping of each, the 3D tunnel. Top first
# ---------------------------------------------------------------------------------------------------------------------
def generator(name, sh, params=None, opacity=0.0, fx=()):
    """A shader of NyxMap's library, with its own effects (fx: (file, its values, its mix)) on its own layer."""
    lid = new_id()
    p = {"opacity": opacity, "blend_mode": "add"}
    p.update({f"param/{k}": v for k, v in (params or {}).items()})
    effects = []
    for file, values, mix in fx:
        seg = file.replace(".fs", "").replace(" ", "_")
        effects.append({"path": file})
        p[f"fx/{seg}/enable"] = True
        p[f"fx/{seg}/mix"] = mix
        for k, v in values.items():
            p[f"fx/{seg}/param/{k}"] = v
    layers.append({"id": lid, "name": name, "enable": True, "locked": False, "viewports": {}, "fx": effects,
                   "source": {"type": "isf", "path": f"{sh}.fs", "width": W, "height": H}, "params": p})
    return lid


SPIRAL = generator("Spiral", "Spiral", {"rotation": 0.5, "count": 6, "width": 0.09, "softness": 0.5, "color1/r": 0.75, "color1/g": 0.95, "color1/b": 1, "color1/a": 1,
                                       "color2/r": 0, "color2/g": 0, "color2/b": 0, "color2/a": 0}, opacity=0.0,
                   fx=[("Kaleidoscope.fs", {"segments": 6, "spin": 0.0, "zoom": 1.0}, 0.0)])
STAR = generator("Poly Star", "Poly Star", {"pointCount": 5, "buldge": 0.2, "pointRadiusInside": 0.32, "pointRadiusOutside": 0.48, "pointRotation": 0,
                                           "starColor/r": 1, "starColor/g": 0.05, "starColor/b": 0.1, "starColor/a": 1}, opacity=0.0)
THREAD = generator("Thread", "Fil", {"x": 0.5, "reach": 1, "amp": 0.05, "width": 0.0008, "glow": 0.2, "count": 1, "spread": 0.012}, opacity=1)
BARS = generator("Snare Bars", "Light Bars", {"count": 9, "width": 0.05, "speed": 0, "angle": 0, "mirror": True, "spread": 1})
WAVES = generator("Bass Lines", "Waveform Lines", {"lines": 34, "amplitude": 1.1, "depth": 0.8, "tilt": 0.4, "thickness": 1.1})
WEB = generator("Hat Lines", "Random Lines", {"lineCount": 14, "lineWidth": 0.0012, "randomSeed": 1, "wobbleAmount": 0, "hueRange": 0, "colorSaturation": 0},
                fx=[("VHS Glitch.fs", {}, 0.0)])

STAGE = new_id()
layers.append({"id": STAGE, "name": "Tunnel", "enable": True, "locked": False, "viewports": {}, "group": True, "scene3d": True, "collapsed": True, "source": {"type": "none"}, "fx": [],
               "params": {"opacity": 1, "blend_mode": "normal", "camera/distance": 14, "camera/fov": 50, "camera/pitch": 0, "camera/yaw": 0, "camera/target/x": 0, "camera/target/y": 0, "camera/target/z": -12,
                          "scene/background/r": 0, "scene/background/g": 0, "scene/background/b": 0, "scene/background/a": 1, "scene/studio": 0, "scene/ambient_intensity": 0.0, "scene/exposure": 1.1,
                          "scene/glow": 1.3, "scene/glow_threshold": 0.5, "scene/glow_size": 0.9, "scene/contrast": 1.2, "scene/saturation": 1.1, "scene/vignette": 0.5, "scene/reflections": 1,
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


FRAME_COUNT = FRAMES
HALF_W, HALF_H = 4.4, 2.5
THICK = 0.045
frames = []
for k in range(FRAMES):
    group = merge(f"Frame {k + 1}", (0, 0, 0))
    bars = [model(f"Frame {k + 1} Top", "Cube", group, (0, HALF_H, 0), axes=(2 * HALF_W, THICK, THICK)), model(f"Frame {k + 1} Bottom", "Cube", group, (0, -HALF_H, 0), axes=(2 * HALF_W, THICK, THICK)),
            model(f"Frame {k + 1} Left", "Cube", group, (-HALF_W, 0, 0), axes=(THICK, 2 * HALF_H, THICK)), model(f"Frame {k + 1} Right", "Cube", group, (HALF_W, 0, 0), axes=(THICK, 2 * HALF_H, THICK))]
    frames.append((group, bars))
FLOOR = model("Floor", "Plane", position=(0, -2.9, -12), rotation=(-90, 0, 0), scale=40, color=(0.02, 0.02, 0.03), metal=0.9, rough=0.2, unlit=False)
WHITE, COLD, RED, VIOLET = (1, 1, 1), (0.72, 0.88, 1), (1, 0.04, 0.05), (0.7, 0.2, 1)
KICK_LIGHTS = [spot("Kick Left", (-4, 2.4, 3), (1.2, -2.8, -14), COLD, angle=8), spot("Kick Right", (4, 2.4, 3), (-1.2, -2.8, -14), COLD, angle=8)]
SNARE_LIGHTS = [spot(f"Snare {k + 1}", (x, 2.45, -3 - 5 * k), (-x, -2.8, -8 - 5 * k), WHITE, angle=10) for k, x in enumerate((-3.6, 3.6, -3.6, 3.6))]
WOB_LIGHTS = [spot(f"Bass {k + 1}", (x, -2.4, -2 - 6 * k), (x * 0.2, 2.5, -7 - 6 * k), RED if k != 1 else VIOLET, angle=13, rays=2.2) for k, x in enumerate((3.8, -3.8, 3.8))]
HAT_LIGHT = spot("Rim", (0, 2.45, -1), (0, -2.8, -22), WHITE, angle=4, rays=1.2)
HORN_LIGHT = spot("Horn", (0, 2.4, 2), (0, 0, -24), WHITE, angle=22, rays=2)


def colour(bars, t, level, curve="hold"):
    for lid in bars:
        for a in "rgb":
            key(lid, f"model/material/color/{a}", t, round(level, 3), curve)


def wave(t, strength=1.0, back=False, base=0.04, fall=0.5, speed=0.04):
    """A pulse runs down the tunnel: the frames light one after the other."""
    for k, (group, bars) in enumerate(frames):
        when = t + (FRAMES - 1 - k if back else k) * speed
        colour(bars, when, base + strength * (1 - 0.4 * k / FRAMES) * 2.4)
        colour(bars, when + fall, base, 0)


for group, bars in frames:
    colour(bars, 0, 0.0)


# The tunnel flies: its speed by section (units per second), each frame at its own place along the loop. A frame wraps
# behind the camera, where it cannot be seen; its keys are linear, so only the changes of speed and the wraps are keyed.
SPEEDS = [(INTRO, BUILD, 1.0), (BUILD, DROP_A, 3.5), (DROP_A, BREAK, 8.0), (BREAK, DROP_B, 0.5), (DROP_B, END, 12.0), (END, BARS_TOTAL, 1.5)]
SEGMENTS = []  # (start s, end s, speed, distance at start)
_d = 0.0
for a, b, v in SPEEDS:
    SEGMENTS.append((a * BAR, b * BAR, v, _d))
    _d += (b - a) * BAR * v


def travelled(t):
    for a, b, v, d in SEGMENTS:
        if t <= b:
            return d + v * (max(t, a) - a)
    return SEGMENTS[-1][3] + SEGMENTS[-1][2] * (SEGMENTS[-1][1] - SEGMENTS[-1][0])


def frame_z(k, t):
    return NEAR - LOOP + ((travelled(t) + k * GAP) % LOOP)


for k, (group, bars) in enumerate(frames):
    times = {0.0, TOTAL}
    for a, b, v, d in SEGMENTS:
        times.add(a)
        times.add(b)
        lo, hi = d + k * GAP, d + v * (b - a) + k * GAP
        m = math.floor(lo / LOOP) + 1
        while m * LOOP <= hi:
            tw = a + (m * LOOP - lo) / v
            if a < tw <= b:
                key(group, "merge/position/z", tw - 0.001, round(frame_z(k, tw - 0.001), 4), 0)
                key(group, "merge/position/z", tw + 0.000001, round(frame_z(k, tw + 0.000001), 4), 0)
            m += 1
    for t in sorted(times):
        if t <= TOTAL:
            key(group, "merge/position/z", t, round(frame_z(k, t), 4), 0)
    # Each frame turns on its own, a half turn each way every two bars, one way and then the other
    for bar in range(0, BARS_TOTAL + 1, 2):
        sign = 1 if (bar // 2 + k) % 2 == 0 else -1
        key(group, "merge/rotation/z", bar * BAR, round(150 * sign, 2), 0)


# ---------------------------------------------------------------------------------------------------------------------
# The score: 28 bars at 140. Intro, build, drop A, break, drop B, end
# ---------------------------------------------------------------------------------------------------------------------
key(AIR, "synth/gate", 0, 1)
key(AIR, "synth/gate", TOTAL - 1.4, 0)
for bar, cut in ((0, 500), (BUILD, 1800), (BREAK, 3200), (DROP_B, 1400), (END, 300)):
    key(AIR, f"audio_fx/{AIR}-bandpass/cutoff", bar * BAR, cut, 0)
key(RISE, "volume", 0, 0)
key(WOB, f"audio_fx/{WOB}-lowpass/cutoff", 0, 350, 0)
key(WOB_HI, f"audio_fx/{WOB_HI}-lowpass/cutoff", 0, 900, 0)

# The intro: the air, a sub drone on E, the thread growing, and the tunnel far away
bass(at(INTRO, 0), 16, E1)
key(THREAD, "param/reach", 0, 0.1, "hold")
key(THREAD, "param/reach", at(BUILD) - BEAT, 1.0, 0)
for bar in range(INTRO, BUILD):
    for s in (0, 8):
        hit(HAT, at(bar, s), 0.02)
    key(SPIRAL, "param/count", bar * BAR, 2 + bar, 0)

# The build: the kick comes in on every beat, the snare roll rises, the riser lifts, the camera comes in
for bar in range(BUILD, DROP_A):
    for s in range(0, 16, 4):
        kick(at(bar, s))
    for s in range(0, 16, 2 if bar < DROP_A - 1 else 1):
        if bar < DROP_A - 1 or s >= 8:
            hit(HAT, at(bar, s), 0.012)
    bass(at(bar, 0), 4, E1 if bar % 2 == 0 else G1)
    bass(at(bar, 8), 4, A1 if bar % 2 == 0 else B1)
key(RISE, "synth/gate", at(BUILD), 1)
key(RISE, "synth/gate", at(DROP_A) - S16, 0)
key(RISE, f"audio_fx/{RISE}-bandpass/cutoff", at(BUILD), 350, "hold")
key(RISE, f"audio_fx/{RISE}-bandpass/cutoff", at(DROP_A) - S16, 7000, 0)
for s in range(8, 16):
    snare(at(DROP_A - 1, s))
    key(SNARE, "volume", at(DROP_A - 1, s), round(0.3 + 0.08 * (s - 8), 2))
key(SNARE, "volume", at(DROP_A), 1)

# Drop A: the hip hop beat with its swing, the sub walking on its riff, the wobble swept four times a bar
RIFF_A = [(0, 5, E1), (6, 2, G1), (8, 3, A1), (12, 2, G1), (14, 2, E1)]
RIFF_B = [(0, 5, A1), (6, 2, B1), (8, 2, E1), (10, 2, G1), (12, 4, E1)]
for bar in range(DROP_A, BREAK):
    for s in (0, 6, 10, 14 if bar % 2 else 11):
        kick(at(bar, s))
    for s in (4, 12):
        snare(at(bar, s))
    for s in range(0, 16, 2):
        hit(HAT, at(bar, s), 0.012)
    if bar % 2:
        hit(OPEN, at(bar, 14), 0.05)
    for s, n, f in (RIFF_A if bar % 2 == 0 else RIFF_B):
        bass(at(bar, s), n, f)
# The wobble: four cycles a bar on the low and on the high one
wobble_t0, wobble_t1 = DROP_A * BAR, BREAK * BAR
rate = 4 / BAR
t = wobble_t0
while t < wobble_t1:
    lfo = 0.5 + 0.5 * math.sin(2 * math.pi * rate * (t - wobble_t0))
    key(WOB, f"audio_fx/{WOB}-lowpass/cutoff", t, round(120 + 1700 * lfo ** 2, 1), 0)
    key(WOB_HI, f"audio_fx/{WOB_HI}-lowpass/cutoff", t, round(300 + 2200 * lfo, 1), 0)
    t += 1 / 48
key(WOB, f"audio_fx/{WOB}-lowpass/cutoff", wobble_t1, 350, 0)
key(WOB_HI, f"audio_fx/{WOB_HI}-lowpass/cutoff", wobble_t1, 900, 0)
for bar in range(DROP_A, BREAK):
    wave(at(bar, 0), 1.0, fall=0.45, speed=0.04)
    wave(at(bar, 8), 0.6, back=True, fall=0.35, speed=0.04)

# Break: the bass alone, horn stabs of the sound system, the snare roll that opens the drop
bass(at(BREAK, 0), 8, E1)
for s in (0, 2, 4):
    hit(HORN, at(BREAK, s), 0.12)
hit(HORN, at(BREAK + 1, 7), S16 * 0.9)
for s in range(8, 16):
    snare(at(BREAK + 1, s))
    key(SNARE, "volume", at(BREAK + 1, s), round(0.2 + 0.1 * (s - 8), 2))
    wave(at(BREAK + 1, s), 0.2 + 0.1 * (s - 8), fall=0.14, speed=0.016)
key(SNARE, "volume", at(DROP_B), 1)
key(RISE, "synth/gate", at(BREAK) + 0.02, 1)
key(RISE, "synth/gate", at(DROP_B) - S16, 0)
key(RISE, f"audio_fx/{RISE}-bandpass/cutoff", at(BREAK), 350, "hold")
key(RISE, f"audio_fx/{RISE}-bandpass/cutoff", at(DROP_B) - S16, 6000, 0)
key(RISE, "volume", at(BREAK), 0, "hold")
key(RISE, "volume", at(DROP_B) - S16, 1, 0)
key(RISE, "volume", at(DROP_B), 0, "hold")
for s in (0, 2, 4):
    hit(HORN, at(BREAK + 1, s), 0.12)
# The break spins: the camera turns, the spiral's kaleidoscope opens
key(STAGE, "camera/yaw", at(BREAK), 0, 0)
key(STAGE, "camera/yaw", at(DROP_B) - BEAT, 180, 0)
key(SPIRAL, "fx/Kaleidoscope/mix", at(BREAK), 0, 0)
key(SPIRAL, "fx/Kaleidoscope/mix", at(DROP_B) - BEAT, 0.8, 0)

# Drop B: the same beat, doubled: hats on every 16th, the wobble eight times a bar, and the lead on the 8ths
LEAD_LINE = [E4, G4, B4, E5, D5, B4, G4, B4]
for bar in range(DROP_B, END):
    for s in (0, 6, 10, 14):
        kick(at(bar, s))
    for s in (4, 12):
        snare(at(bar, s))
    for s in range(16):
        hit(HAT, at(bar, s), 0.01)
    hit(OPEN, at(bar, 14), 0.05)
    for s, n, f in (RIFF_A if bar % 2 == 0 else RIFF_B):
        bass(at(bar, s), n, f)
    for n in range(8):
        s = 2 * n
        key(LEAD, "synth/frequency", at(bar, s) - 0.002, LEAD_LINE[n])
        key(LEAD, "synth/index", at(bar, s) - 0.002, 2.0 if n % 2 == 0 else 6.5)
        hit(LEAD, at(bar, s), 0.09)
    wave(at(bar, 0), 1.0, fall=0.4, speed=0.035)
    wave(at(bar, 8), 0.7, back=True, fall=0.3, speed=0.035)
# The wobble, eight cycles a bar
wobble_t0, wobble_t1 = DROP_B * BAR, END * BAR
rate = 8 / BAR
t = wobble_t0
while t < wobble_t1:
    lfo = 0.5 + 0.5 * math.sin(2 * math.pi * rate * (t - wobble_t0))
    key(WOB, f"audio_fx/{WOB}-lowpass/cutoff", t, round(100 + 1900 * lfo ** 3, 1), 0)
    key(WOB_HI, f"audio_fx/{WOB_HI}-lowpass/cutoff", t, round(300 + 2600 * lfo, 1), 0)
    t += 1 / 48
key(WOB, f"audio_fx/{WOB}-lowpass/cutoff", wobble_t1, 300, 0)
key(WOB_HI, f"audio_fx/{WOB_HI}-lowpass/cutoff", wobble_t1, 900, 0)
key(SPIRAL, "fx/Kaleidoscope/mix", DROP_B * BAR, 1.0, 0)

# The end: the siren climbs to the last bar, the last hit on the kick and the snare, the boom rings out
END_HIT = 27 * BAR
key(SIREN, "synth/gate", at(END, 0), 1)
key(SIREN, "synth/gate", END_HIT - 0.05, 0)
n, t = 0, at(END, 0)
while t < END_HIT:
    key(SIREN, "synth/frequency", t, round(470 + 380 * (t - at(END, 0)) / (END_HIT - at(END, 0)), 1), 0)
    t += BEAT / 4
    n += 1
kick(END_HIT)
snare(END_HIT)
hit(BOOM, END_HIT, 0.05)
key(BOOM, "synth/frequency", END_HIT - 0.002, 110)
key(BOOM, "synth/frequency", END_HIT + 0.6, 34, 0)
for s in (0, 4, 8):
    hit(HORN, at(END + 1, s), 0.12)
wave(END_HIT, 1.6, fall=2.0, speed=0.05, base=0)
key(THREAD, "param/reach", END_HIT + 0.4, 1.0, "hold")
key(THREAD, "param/reach", TOTAL - 0.1, 0.0, 0)
key(STAGE, "scene/fog/density", at(END, 0), 0.09, 0)
key(STAGE, "scene/fog/density", TOTAL, 0.25, 0)

# The mapping moves with the music: the corners of the pictures pulled by swells, the bars and the star turning
sine(WAVES, "corner/top_left/y", DROP_A * BAR, BREAK * BAR, 2, 22, 0, 0)
sine(WAVES, "corner/bottom_right/y", DROP_A * BAR, BREAK * BAR, 2, 22, 0, math.pi)
sine(WAVES, "corner/top_right/x", DROP_A * BAR, BREAK * BAR, 2, 14, 0, math.pi / 2)
sine(WAVES, "corner/bottom_left/x", DROP_A * BAR, BREAK * BAR, 2, 14, 0, -math.pi / 2)
sine(WAVES, "corner/top_left/y", DROP_B * BAR, END * BAR, 4, 34, 0, 0)
sine(WAVES, "corner/bottom_right/y", DROP_B * BAR, END * BAR, 4, 34, 0, math.pi)
sine(WAVES, "corner/top_right/x", DROP_B * BAR, END * BAR, 4, 22, 0, math.pi / 2)
sine(WAVES, "corner/bottom_left/x", DROP_B * BAR, END * BAR, 4, 22, 0, -math.pi / 2)
sine(BARS, "rotation", DROP_A * BAR, BREAK * BAR, 1, 18, 0, 0)
sine(BARS, "rotation", DROP_B * BAR, END * BAR, 2, 26, 0, 0)
sine(SPIRAL, "corner/top_left/x", 0, TOTAL, 1 / 4, 8, 0, 0)
sine(SPIRAL, "corner/bottom_right/x", 0, TOTAL, 1 / 4, 8, 0, math.pi)
sine(SPIRAL, "param/rotation", 0, TOTAL, 1 / 16, 0.5, 0.5, 0)
key(STAR, "rotation", 0, 0, 0)
key(STAR, "rotation", TOTAL, 1440, 0)
sine(WEB, "position/x", BUILD * BAR, END * BAR, 2, 90, 0, 0)
# The camera: swells of the target in the drops, and a shot for each part, pulled in on the build
key(STAGE, "camera/target/x", 0, 0, 0)
sine(STAGE, "camera/target/x", DROP_A * BAR, BREAK * BAR, 2, 1.6, 0, 0)
sine(STAGE, "camera/target/x", DROP_B * BAR, END * BAR, 4, 1.2, 0, 0)
SHOTS = [(INTRO, 14, 0, 0, 0, -12, 50), (BUILD, 10, -4, 10, 0, -12, 58), (DROP_A, 12, 6, -12, 0, -12, 66), (BREAK, 8, 0, 0, 0, -12, 52),
         (DROP_B, 13, -8, 180, 0, -12, 74), (END, 14, 0, 0, 0, -12, 40)]
for n, (bar, dist, pitch, yaw, ty, tz, fov) in enumerate(SHOTS):
    t = bar * BAR
    key(STAGE, "camera/distance", t, dist, 3)
    key(STAGE, "camera/pitch", t, pitch, 3)
    if n != 3:  # the break's yaw is its own turn
        key(STAGE, "camera/yaw", t, yaw, 3)
    key(STAGE, "camera/target/y", t, ty, 3)
    key(STAGE, "camera/target/z", t, tz, 3)
    key(STAGE, "camera/fov", t, fov, 3)
key(STAGE, "scene/fog/density", 0, 0.05, 0)
key(STAGE, "scene/fog/density", BUILD * BAR, 0.1, 0)
key(STAGE, "scene/fog/density", DROP_B * BAR, 0.14, 0)


# ---------------------------------------------------------------------------------------------------------------------
# Each voice's gate, an envelope on what it lights: a pulse falls with its note. Mapping and FX follow the same way
# ---------------------------------------------------------------------------------------------------------------------
def follow(source, layer, param, low, high, decay, attack=0.001, sustain=0.0):
    n = len(interactions) + 1
    interactions.append({"id": str(n), "name": f"Gate → {param}", "enabled": True, "source": "internal", "source_layer": source, "source_param": "synth/gate", "layer": layer, "param": param, "master": 1, "takeover": "jump",
                         "filters": [{"type": "envelope", "a": attack, "b": decay, "c": sustain, "d": 0.04}, {"type": "remap", "a": 0, "b": 1, "c": low, "d": high}]})


follow(KICK, KICK, "synth/frequency", 52, 180, 0.04)
follow(KICK, WOB, "volume", 0.6, 1, 0.14)
follow(SUB, WOB_LIGHTS[0], "light/intensity", 0, 600, 0.3, attack=0.006)
follow(SUB, WOB_LIGHTS[2], "light/intensity", 0, 600, 0.3, attack=0.006)
follow(LEAD, WOB_LIGHTS[1], "light/intensity", 0, 900, 0.15, attack=0.002)
for lid in KICK_LIGHTS:
    follow(KICK, lid, "light/intensity", 0, 1000, 0.4)
for lid in SNARE_LIGHTS:
    follow(SNARE, lid, "light/intensity", 0, 1400, 0.28)
follow(HAT, HAT_LIGHT, "light/intensity", 0, 500, 0.08)
follow(OPEN, HAT_LIGHT, "light/level", 1, 2.5, 0.2)
follow(HORN, HORN_LIGHT, "light/intensity", 0, 900, 0.12, sustain=0.6)
follow(KICK, THREAD, "param/glow", 0.2, 2.8, 0.35)
follow(SUB, THREAD, "param/amp", 0.03, 0.26, 0.5, attack=0.01, sustain=0.5)
follow(KICK, STAR, "scale/x", 100, 150, 0.14)
follow(KICK, STAR, "scale/y", 100, 150, 0.14)
follow(LEAD, STAR, "opacity", 0, 0.9, 0.09)
follow(KICK, SPIRAL, "param/count", 4, 12, 0.2)
follow(KICK, SPIRAL, "opacity", 0.25, 0.9, 0.2)
follow(SNARE, BARS, "opacity", 0, 1, 0.2)
follow(SNARE, WEB, "fx/VHS_Glitch/mix", 0, 0.9, 0.1)
follow(HAT, WEB, "opacity", 0, 0.5, 0.05)
follow(WOB, WAVES, "opacity", 0, 0.95, 0.4, attack=0.01, sustain=0.22)
follow(LEAD, SPIRAL, "opacity", 0.0, 0.8, 0.1)
follow(KICK, STAGE, "scene/glow", 1.3, 3.2, 0.28)
follow(SNARE, STAGE, "scene/exposure", 1.1, 2.2, 0.16)

layers.extend(SOUND_LAYERS)
timeline_tracks = []
for (layer, param), keys in tracks.items():
    keys.sort(key=lambda k: k[0])
    timeline_tracks.append({"layer": layer, "param": param, "keys": keys})
timelines = [{"id": "1", "name": "Vertigo Pressure", "duration": TOTAL, "loop": "once", "tracks": timeline_tracks}]
states = [dict(copy.deepcopy(layer), included=True) for layer in layers]
snapshot = {"id": "1", "name": "Ready", "fade": 0, "layers": states, "composition": {"included": True, "opacity": 1, "volume": 0.8}, "interactions": [{"id": i["id"], "included": True, "enabled": True} for i in interactions]}
steps = [{"text": "GO", "snapshot": "1", "description": "The whole thing plays from here, as it is made", "continue": "follow"}, {"text": "Vertigo Pressure", "timeline": "1", "action": "play"}]
project = {"app": "NyxMap", "format_version": 1, "composition": {"width": W, "height": H, "fps": 60, "speed": 1}, "audio": {"volume": 0.8}, "layers": layers, "interactions": interactions, "timelines": timelines, "presets": [], "snapshots": [snapshot], "snapshot_tree": ["1"],
           "sequences": [{"id": "1", "name": "Vertigo Pressure", "loop": False, "steps": steps}], "current_sequence": 0, "startup_sequence": 0, "startup_step": 0, "startup_go": "--autoplay" in sys.argv, "ui": {"selected_layer": 0}}
(HERE / "Vertigo Pressure.nyxmap").write_text(json.dumps(project, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
print(f"{len(layers)} layers, {len(interactions)} interactions, {sum(len(t['keys']) for t in timeline_tracks)} keys on {len(timeline_tracks)} tracks, {TOTAL:.1f} s")
