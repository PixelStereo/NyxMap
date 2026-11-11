#!/usr/bin/env python3
"""Build Camera Inversa for NyxMap using only the Python standard library."""

import json
import math
import random
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIO = ROOT / "audio"
AUDIO.mkdir(exist_ok=True)


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def bowed_metal():
    """Excite an inharmonic resonator bank with filtered friction noise."""
    sample_rate = 24000
    rng = random.Random(151)
    modes = [(173, 1), (274, .67), (439, .52), (711, .38), (1123, .31),
             (1681, .22), (2369, .13), (3817, .09)]
    coeffs = [2 * .997 * math.cos(math.tau * hz / sample_rate) for hz, _ in modes]
    states = [(0.0, 0.0)] * len(modes)
    frames = bytearray()
    for i in range(sample_rate * 4):
        t = i / sample_rate
        pressure = math.sin(math.pi * t / 4) ** .6
        friction = (rng.random() - .5) * (.45 + .55 * math.sin(math.tau * 19 * t) ** 2)
        value = 0.0
        for m, ((_, gain), coefficient, (last, older)) in enumerate(zip(modes, coeffs, states)):
            sample = friction + coefficient * last - .997**2 * older
            states[m] = sample, last
            value += gain * sample
        frames.extend(struct.pack("<h", round(14000 * pressure * math.tanh(value * .07))))
    with wave.open(str(AUDIO / "bowed-metal.wav"), "wb") as out:
        out.setparams((1, 2, sample_rate, 0, "NONE", "not compressed"))
        out.writeframes(frames)


def source(kind, path, **extra):
    return dict(type=kind, path=path, relative_path=path, **extra)


def layer(id, name, media, params, audio_fx=()):
    state = dict(id=id, name=name, enable=True, locked=False, viewports={}, fx=[],
                 play=True, source=media, params=params)
    if audio_fx:
        state["audio_fx"] = list(audio_fx)
    return state


def effect(id, kind, wet, **values):
    return dict(id=id, kind=kind, enabled=True, wet=wet, values=values)


def voice(id, key, name, kind, effects=(), **recipe):
    filename = f"{key}.nyxaudio"
    write_json(AUDIO / filename, dict(version=1, type=kind, **recipe))
    return layer(id, name, source("audio", f"audio/{filename}"),
                 {"play": True, "play_mode": "loop", "volume": 1 if key == "strike" else 0, "audio_fx/enable": True}, effects)


def library_shader(filename, section="generators"):
    return dict(path=filename, relative_path=f"../../isf/{section}/{filename}")


def axes(prefix, keys, values):
    return {f"{prefix}/{axis}": value for axis, value in zip(keys, values)}


WORLD, STAGE, SMOKE = "101", "107", "108"
FRAME, IRIS, PLATES, RGB, SHARDS = "109", "110", "113", "114", "115"
LAMP, BACK = "111", "112"
AIR, METAL, SUB, STRIKE, BREAK, KICK = "102", "103", "104", "105", "106", "116"

visuals = [
    dict(layer(WORLD, "Camera Inversa · aperture", dict(type="none"), {"opacity": 1}),
         group=True, collapsed=True),
    dict(layer(SMOKE, "Smoke · optical residue", dict(type="isf", width=768, height=432,
              **library_shader("smoke.fs")), {"blend_mode": "add", "opacity": .4}),
         parent=WORLD),
    dict(layer(STAGE, "Optical chamber · 3D", dict(type="none"), {
        "scene/studio": .55, "scene/ambient_intensity": .11, "scene/exposure": .95,
        "scene/glow": .25, "scene/glow_threshold": .85, "scene/vignette": .06,
        "scene/fog/reach": 24, "scene/fog/falloff": .12,
        **axes("scene/fog/color", "rgb", (.065, .065, .065)),
        **axes("scene/background", "rgba", (0, 0, 0, 1)),
    }), parent=WORLD, group=True, scene3d=True, collapsed=True),
]


def assembly(id, name, parent=STAGE, **params):
    visuals.append(dict(layer(id, name, dict(type="none"), params),
                        parent=parent, group=True, merge3d=True, collapsed=True))


materials = {}


def model(name, parent, shape, position, scale, color, rotation=(0, 0, 0), lit=False):
    id = str(500 + len(visuals))
    rgb = (color,) * 3 if isinstance(color, (int, float)) else color
    if rgb not in materials:
        material_id = str(1000 + len(materials))
        materials[rgb] = dict(id=material_id, name=name + " · finish", metal=.28, rough=.34,
                              **dict(zip("rgb", rgb)))
    media = dict(type="model", path=f"/NyxMap/3D/{shape}", material=materials[rgb]["id"])
    visuals.append(dict(layer(id, name, media, {
        "model/fit": True, "model/detail": 2 if shape in {"Sphere", "Torus"} else 1,
        "model/unlit": not lit,
        **axes("model/position", "xyz", position),
        **axes("model/rotation", "xyz", rotation),
        **axes("model/scale", "xyz", scale),
    }), parent=parent))
    return id


def frame(parent, width, height, z, stroke, color, lit=False):
    for side in (-1, 1):
        model("Horizontal edge", parent, "Cube", (0, side * height / 2, z),
              (width + stroke, stroke, stroke * 2), color, lit=lit)
        model("Vertical edge", parent, "Cube", (side * width / 2, 0, z),
              (stroke, height, stroke * 2), color, lit=lit)


assembly(FRAME, "01 · Folding ribs")
ribs = []
for k in range(8):
    rib = str(300 + k)
    ribs.append(rib)
    assembly(rib, f"Rib {k + 1:02}", FRAME, **{"merge/scale/all": 1 + k * .048})
    frame(rib, 5.4, 3.0, 0, .07 if k % 3 else .14, .91 - .075 * k, lit=k % 3 == 0)

assembly(IRIS, "02 · False iris")
for k in range(5):
    model(f"Turned ring {k + 1}", IRIS, "Torus", (0, 0, -.45 * k),
          (3.65 - .56 * k, .49, 3.65 - .56 * k), .26 + .095 * k,
          (90 + 6 * k, 9 * (-1)**k, 0), lit=True)
CORE = model("Obsidian pupil", IRIS, "Sphere", (0, 0, -.68), (1.07, 1.07, .72), .13, lit=True)

assembly(PLATES, "03 · Photographic negative")
for k in range(11):
    x = (k - 5) * .47
    model(f"Black fin {k + 1:02}", PLATES, "Cube", (x, 0, -abs(x) * .23),
          (.21 + .08 * (k % 3), 5.2 - .25 * abs(k - 5), .56), .012 + .005 * (k % 3),
          (0, 11 * (-1)**k, 17 - 3.4 * k))

assembly(RGB, "04 · Separated primaries")
channels = []
for k, (name, color) in enumerate((("Red", (1, .018, .05)), ("Green", (.04, 1, .22)), ("Blue", (.12, .26, 1)))):
    channel = str(320 + k)
    channels.append(channel)
    assembly(channel, name, RGB)
    frame(channel, 5.6, 2.8, -.20 * k, .095, color)
    for line in range(4):
        model("Registration line", channel, "Cube", (0, -1.28 + line * .19, -.20 * k),
              (2.3 + line * .64, .023, .06), color)

assembly(SHARDS, "05 · Contact debris")
rng = random.Random(417)
for k in range(20):
    angle = k * 2.39996
    radius = rng.uniform(.7, 3.7)
    model(f"Splinter {k + 1:02}", SHARDS, "Cube",
          (math.cos(angle) * radius, math.sin(angle) * radius * .72, rng.uniform(-3.8, 1.1)),
          (rng.uniform(.34, 1.65), rng.uniform(.018, .08), .055),
          rng.uniform(.24, .84), (rng.uniform(-60, 60), rng.uniform(-50, 50), rng.uniform(-80, 80)))

for id, name, position, aim in (
    (LAMP, "Raking key", (-3.2, 3.4, 4), (0, 0, -2)),
    (BACK, "Cross beam", (3.1, -1.5, -2.8), (-1, 1.4, -5)),
):
    visuals.append(dict(layer(id, name, dict(type="light"), {
        "light/type": "spot", "light/lookat": True, "light/angle": 24,
        "light/intensity": 27, "light/rays": .16, "light/shadow": False,
        **axes("light/position", "xyz", position), **axes("light/lookpoint", "xyz", aim),
    }), parent=STAGE))

bowed_metal()
layers = visuals + [
    voice(AIR, "air", "Breath / hiss", "pink",
          [effect("air-filter", "bandpass", 1, cutoff=780, resonance=1.5)],
          gain=.16, attack=.3, sustain=1, release=.26, gate=1),
    voice(METAL, "metal", "Bowed steel grains", "granular",
          [effect("metal-room", "reverb", .28, room=.83, damping=.5)],
          source="bowed-metal.wav", grainMs=110, density=12, pitch=1, gain=.26,
          attack=.06, sustain=1, release=.24, gate=1),
    voice(SUB, "sub", "Asymmetric sub", "fm",
          [effect("sub-filter", "lowpass", 1, cutoff=245, resonance=.78)],
          frequency=44, ratio=1.006, index=.54, gain=.31,
          attack=.013, decay=.18, sustain=.66, release=.22, gate=0),
    voice(STRIKE, "strike", "Live metal strike", "fm",
          [effect("strike-room", "reverb", .31, room=.78, damping=.48)],
          frequency=270, ratio=2.41, index=4.6, gain=.15,
          attack=.003, decay=.24, sustain=.12, release=.32, gate=0),
    voice(BREAK, "break", "Dry contact breaks", "white",
          [effect("break-band", "bandpass", 1, cutoff=1800, resonance=1.9),
           effect("break-drive", "distortion", .6, drive=.44, tone=3000)],
          gain=.32, attack=.002, decay=.053, sustain=.1, release=.055, gate=0),
    voice(KICK, "kick", "Impact / membrane", "fm",
          [effect("kick-filter", "lowpass", 1, cutoff=720, resonance=.8)],
          frequency=49, ratio=1.56, index=.8, gain=.35,
          attack=.002, decay=.12, sustain=.04, release=.09, gate=0),
]

# Cue faders set the mix and camera; animation writes to different parameters.
scene_controls = [
    ("image", "01 · Aperture level", [(WORLD, "opacity", 0, 1), (STRIKE, "volume", 0, 1)]),
    ("smoke", "02 · Smoke mass", [(SMOKE, "param/densityAmount", 0, 1.8)]),
    ("angle", "03 · Camera orbit", [(STAGE, "camera/yaw", -80, 80)]),
    ("depth", "04 · Relief ↔ cavity",
     [(rib, "merge/position/z", k * .39, -k * .93) for k, rib in enumerate(ribs)]),
    ("air", "05 · Air level", [(AIR, "volume", 0, .76)]),
    ("metal", "06 · Granular level", [(METAL, "volume", 0, .83)]),
    ("sub", "07 · Sub pressure", [(SUB, "volume", 0, .96)]),
    ("fracture", "08 · Contact noise", [(BREAK, "volume", 0, .92)]),
    ("impact", "09 · Impact level", [(KICK, "volume", 0, .86)]),
    ("density", "10 · Grain density", [(METAL, "synth/density", 2, 70)]),
    ("size", "11 · Grain duration", [(METAL, "synth/grainMs", 18, 245)]),
]
live_controls = [
    ("drift", "12 · Smoke turbulence", .36, [(SMOKE, "param/turbulence", 0, 1)]),
    ("pitch", "13 · Grain transposition", .45, [(METAL, "synth/pitch", .38, 1.8)]),
    ("room", "14 · Grain space", .28, [(METAL, "audio_fx/metal-room/wet", 0, .76)]),
    ("twist", "15 · Rib torsion", .5,
     [(rib, "merge/rotation/z", -k * 7.5, k * 7.5) for k, rib in enumerate(ribs)]),
    ("strike", "16 · Strike / hold", 0, [(STRIKE, "synth/gate", 0, 1)]),
]
controls, interactions = [], []


def route(name, target, param, filters, **origin):
    interactions.append(dict(id=str(len(interactions) + 1), name=name, enabled=True,
                             layer=target, param=param, master=1, takeover="jump",
                             filters=filters, **origin))


def add_control(key, name, initial, targets):
    controls.append(dict(id=str(len(controls) + 1), key=key, name=name,
                         kind="button" if key == "strike" else "slider",
                         min=0, max=1, step=.01, default=initial, value=initial))
    for target, param, low, high in targets:
        route(name, target, param, [dict(type="remap", a=0, b=1, c=low, d=high)],
              source="internal", source_layer="0", source_param=f"control/{key}")


def follow(voice_id, target, param, low, high, tail):
    route(f"Voice {voice_id} → {param}", target, param,
          [dict(type="envelope", a=.002, b=tail, c=0, d=.018),
           dict(type="remap", a=0, b=1, c=low, d=high)],
          source="internal", source_layer=voice_id, source_param="synth/gate")


follow(KICK, LAMP, "light/intensity", 23, 95, .34)
follow(BREAK, BACK, "light/intensity", 15, 69, .19)
follow(STRIKE, STAGE, "scene/glow", .24, .94, .42)

# Each shot is a distinct optical setup, not a different exposure of the same view.
# Camera: distance, lens, pitch, aim XYZ. Aperture: X/Y percent of composition.
scenes = [
    (0, "Fessura", "A breath and a sliver of steel. The image opens along one axis.",
     (1,.53,.51,.02,.38,.09,0,0,0,.10,.84), (IRIS,), (8,32,12,(0,.15,0)), (100,11), (.77,.68,.045)),
    (5, "Diaframma", "The slit snaps into a square and the iris rushes the lens.",
     (1,.26,.37,.08,.17,.34,.14,.10,.32,.32,.33), (IRIS,), (5.8,47,-11,(.08,0,-.2)), (58,100), (.42,1.08,.026)),
    (12, "Rilievo", "Wide side light. The ribs fan out into an impossible foreground.",
     (1,.24,.78,0,.10,.26,.68,.24,.65,.31,.39), (FRAME,), (9.1,55,19,(0,0,1.2)), (100,100), (.32,.88,.044)),
    (19, "Negativo", "Hard cut to white and black fins. The low end disappears.",
     (1,0,.52,.5,.08,.57,0,.38,0,.68,.08), (PLATES,), (7.1,48,6,(0,0,0)), (100,100), (0,.72,0)),
    (25, "Segnale rotto", "Primaries shear in separate directions over a broken-bass phrase.",
     (1,.14,.48,.5,.04,.58,.82,.84,.9,.86,.09), (RGB, SHARDS), (9,54,4,(0,0,-.7)), (100,71), (.25,1.2,.023)),
    (33, "Vuoto", "The pulse is severed. Hold on black.",
     (0,0,.5,.5,0,0,0,0,0,.1,.6), (), (9,54,0,(0,0,0)), (100,100), (0,.6,0)),
    (36, "Camera Inversa", "A frontal axis becomes a deep cavity. Accelerate through its lip.",
     (1,.27,.5,1,.15,.36,.76,.32,.83,.43,.48), (FRAME,), (11,45,0,(0,0,-4.6)), (100,100), (.33,.86,.046)),
    (45, "Caduta", "The iris rolls sideways while debris crosses the frame on the beat.",
     (1,.21,.64,.78,.09,.77,.65,.92,1,.96,.04), (IRIS, SHARDS), (6.1,67,-22,(0,0,-.8)), (100,100), (.28,1.22,.031)),
    (53, "Impronta", "The machine collapses into a narrow afterimage and one decaying grain.",
     (1,.55,.52,1,.11,.29,.08,0,.17,.06,.94), (FRAME,), (7.4,29,-2,(0,0,-2.3)), (31,100), (.72,.54,.034)),
    (60, "Nero", "Black and silence.",
     (0,0,.5,1,0,0,0,0,0,.06,.94), (), (8,49,0,(0,0,0)), (100,100), (0,.54,0)),
]

for (key, name, targets), initial in zip(scene_controls, scenes[0][3]):
    # Keep the opening image on standby. GO brings in the soundtrack.
    add_control(key, name, 0 if key in {"air", "metal", "sub", "fracture", "impact"} else initial, targets)
for key, name, initial, targets in live_controls:
    add_control(key, name, initial, targets)


def shot_state(visible, camera, aperture, atmosphere):
    distance, lens, pitch, aim = camera
    opacity, spread, fog = atmosphere
    white = .94 if PLATES in visible else 0
    state = {
        (WORLD, "scale/x"): aperture[0], (WORLD, "scale/y"): aperture[1],
        (STAGE, "camera/distance"): distance, (STAGE, "camera/fov"): lens, (STAGE, "camera/pitch"): pitch,
        (STAGE, "scene/fog/density"): fog, (SMOKE, "opacity"): opacity, (SMOKE, "param/spread"): spread,
        (SMOKE, "param/flow"): .38, (LAMP, "light/level"): .86, (BACK, "light/level"): .65,
        (FRAME, "merge/scale/x"): 1, (FRAME, "merge/rotation/z"): 0,
        (IRIS, "merge/rotation/y"): 0, (IRIS, "merge/rotation/z"): 0,
        (IRIS, "merge/scale/all"): 1, (CORE, "model/position/z"): -.68,
        (PLATES, "merge/rotation/z"): 0, (PLATES, "merge/scale/x"): 1,
        (SHARDS, "merge/rotation/y"): 0, (SHARDS, "merge/rotation/z"): 0,
        (SHARDS, "merge/scale/all"): 1,
        (AIR, "audio_fx/air-filter/cutoff"): 780,
        (BREAK, "audio_fx/break-band/cutoff"): 1800,
        (SUB, "synth/frequency"): 43, (KICK, "synth/frequency"): 55,
    }
    for axis, value in zip("xyz", aim):
        state[(STAGE, f"camera/target/{axis}")] = value
    for axis in "rgb":
        state[(STAGE, f"scene/background/{axis}")] = white
    for id in (FRAME, IRIS, PLATES, RGB, SHARDS):
        state[(id, "enable")] = int(id in visible)
    for id in channels:
        for param in ("merge/position/x", "merge/position/y", "merge/rotation/z"):
            state[(id, param)] = 0
    for id in (SUB, KICK, BREAK):
        state[(id, "synth/gate")] = 0
    return state


def motion(tracks, target, param, *points):
    tracks[target, param].extend(points)


def note(tracks, id, time, length, level, frequency=None):
    motion(tracks, id, "synth/gate", (time, level, "hold"), (round(time + length, 3), 0, "hold"))
    if id == KICK:
        motion(tracks, id, "synth/frequency", (time, 152, "hold"), (round(time + .115, 3), 43, 2))
    elif frequency:
        motion(tracks, id, "synth/frequency", (time, frequency, "hold"))


def rhythm(tracks, impacts, bass, cuts):
    for time in impacts:
        note(tracks, KICK, time, .085, .93)
    for time, length, hz in bass:
        note(tracks, SUB, time, length, .84, hz)
    for index, time in enumerate(cuts):
        note(tracks, BREAK, time, .032, (.9, .48, .71)[index % 3])
        motion(tracks, BREAK, "audio_fx/break-band/cutoff",
               (time, (740, 3350, 1500, 4720, 1080)[index % 5], "hold"))


def score(index, tracks):
    if index == 0:
        motion(tracks, WORLD, "scale/y", (1.2, 11, "hold"), (4.64, 43, 3))
        motion(tracks, STAGE, "camera/distance", (4.64, 6.3, 3))
        motion(tracks, IRIS, "merge/rotation/y", (4.85, -31, 3))
        motion(tracks, AIR, "audio_fx/air-filter/cutoff", (4.6, 1800, 3))
    elif index == 1:
        motion(tracks, STAGE, "camera/fov", (.17, 31, 2), (2.8, 51, 3), (6.65, 66, 3))
        motion(tracks, IRIS, "merge/rotation/y", (.32, 27, 3), (6.83, -39, 3))
        motion(tracks, CORE, "model/position/z", (1.6, .88, 3), (6.7, -.92, 3))
        rhythm(tracks, [.025, 4.62], [(4.67, .54, 47)], [2.65, 3.22, 6.51])
    elif index == 2:
        motion(tracks, STAGE, "camera/target/x", (2.52, -1.55, 3), (6.75, .9, 3))
        motion(tracks, FRAME, "merge/rotation/z", (2.52, -26, 3), (6.72, 38, 3))
        motion(tracks, FRAME, "merge/scale/x", (2.36, 1.36, 3), (6.8, .67, 3))
        rhythm(tracks, [.02, .73, 1.61, 2.68, 3.54, 4.25, 5.83, 6.46],
               [(.08,.49,43), (1.61,.74,38), (3.57,.34,49), (4.28,.61,41), (5.85,.8,36)],
               [.42, 1.21, 2.53, 2.68, 4.14, 5.7, 6.51, 6.61])
    elif index == 3:
        motion(tracks, PLATES, "merge/rotation/z", (.88, 67, 3), (2.02, 90, 3), (3.1, 9, "hold"), (5.9, -36, 3))
        motion(tracks, PLATES, "merge/scale/x", (.9, .16, 2), (2.6, .95, 3), (5.75, .06, 3))
        motion(tracks, STAGE, "camera/fov", (3.11, 25, "hold"), (5.94, 52, 3))
        rhythm(tracks, [], [], [.04, .66, 2.37, 3.1, 3.18, 5.62])
    elif index in (4, 7):
        hits = [.025, .62, 1.12, 1.86, 2.44, 3.29, 3.93, 4.45, 5.37, 6.1, 6.88, 7.44]
        if index == 7:
            hits = [.025, .48, 1.41, 2.08, 2.29, 3.33, 4.07, 4.92, 5.46, 6.15, 7.55]
        cuts = [.13,.36,.5,1.28,1.47,1.64,2.12,2.32,2.67,3.04,3.41,3.57,3.68,
                4.15,4.67,4.81,5.19,5.53,5.71,6.23,6.43,6.59,7.01,7.2,7.53,7.71]
        rhythm(tracks, hits, [(t, min(.39, 8-t-.04), (37, 44, 49, 40)[k % 4])
                             for k, t in enumerate(hits) if k % 4 != 2], cuts)
        for k, time in enumerate(cuts):
            motion(tracks, SHARDS, "merge/rotation/z", (time, (-1)**k * (16 + 3 * (k % 6)), "hold"))
            motion(tracks, SHARDS, "merge/rotation/y", (time, (-1)**(k//2) * (19 + 6 * (k % 5)), "hold"))
            motion(tracks, SHARDS, "merge/scale/all", (time, (1.08, .58, 1.37, .78)[k % 4], "hold"))
        if index == 4:
            for k, id in enumerate(channels):
                for n, time in enumerate([.12, .53, 1.6, 2.4, 3.32, 3.72, 4.53, 5.31, 6.1, 7.25]):
                    offset = (k - 1) * (-1)**n
                    motion(tracks, id, "merge/position/x", (time, offset * (1.04 + .23 * (n % 3)), "hold"))
                    motion(tracks, id, "merge/position/y", (time, -offset * (.20 + .13 * (n % 4)), "hold"))
                    motion(tracks, id, "merge/rotation/z", (time, offset * (8 + 7 * (n % 3)), "hold"))
            motion(tracks, WORLD, "scale/y", (1.84, 100, "hold"), (3.91, 42, "hold"), (4.42, 100, "hold"))
            motion(tracks, STAGE, "camera/target/x", (2.42, 1.25, "hold"), (4.42, -.72, "hold"), (5.38, 0, "hold"))
        else:
            motion(tracks, IRIS, "merge/rotation/y", (1.7, 69, 3), (4.35, -44, "hold"), (7.87, 80, 3))
            motion(tracks, IRIS, "merge/rotation/z", (3.37, -49, 3), (6.45, 114, 3), (7.88, 172, 3))
            motion(tracks, STAGE, "camera/fov", (2.25, 38, "hold"), (4.34, 81, "hold"), (7.56, 24, "hold"))
    elif index == 6:
        motion(tracks, STAGE, "camera/distance", (2.45, 8.7, 3), (5.35, 5.05, 3), (8.9, 2.9, 3))
        motion(tracks, STAGE, "camera/target/z", (5.35, -5.8, 3), (8.9, -8.4, 3))
        motion(tracks, STAGE, "camera/fov", (4.5, 58, 3), (7.9, 78, 3))
        motion(tracks, FRAME, "merge/rotation/z", (3.9, -12, 3), (8.9, 62, 3))
        motion(tracks, FRAME, "merge/scale/x", (6.2, .74, 3), (8.9, 1.23, 3))
        rhythm(tracks, [.025, 1.24, 2.51, 3.94, 4.58, 5.8, 6.72, 7.34, 8.15, 8.68],
               [(.05,.73,37), (1.25,1.0,43), (2.53,.72,39), (3.96,.34,48),
                (4.59,.94,36), (5.81,.79,42), (6.74,.46,49), (7.35,1.37,38)],
               [1.06, 2.34, 3.6, 4.47, 5.65, 6.55, 7.14, 7.24, 7.89, 8.52, 8.76])
    elif index == 8:
        rhythm(tracks, [.025], [(.06,.45,38)], [])
        motion(tracks, WORLD, "scale/x", (1.84, 22, 3), (4.45, 7, 3),
               (4.46, 100, "hold"), (6.9, 100, "hold"))
        motion(tracks, FRAME, "enable", (4.46, 0, "hold"))
        motion(tracks, STAGE, "scene/fog/density", (4.46, 0, "hold"))
        motion(tracks, FRAME, "merge/scale/x", (4.25, .19, 3), (5.62, .025, 3))
        motion(tracks, SMOKE, "opacity", (3.48, .72, 3), (4.46, .88, "hold"), (6.75, 0, 3))
        motion(tracks, SMOKE, "param/flow", (6.32, .06, 3))


def track_list(tracks):
    result = []
    for (id, param), points in tracks.items():
        points = sorted(points, key=lambda key: key[0])
        if param == "enable":
            result.append(dict(layer=id, param=param, type="event",
                               keys=[t for t, _, _ in points], values=[v for _, v, _ in points]))
        else:
            # The score specifies the arrival curve. NyxMap stores it on the previous key.
            curves = [point[2] for point in points[1:]] + ["hold"]
            result.append(dict(layer=id, param=param,
                               keys=[(t, v, curve) for (t, v, _), curve in zip(points, curves)]))
    return result


presets, manual, automatic, clips = [], [], [], []
reference = {}
for index, (at, title, gesture, mix, visible, camera, aperture, atmosphere) in enumerate(scenes):
    id = str(index + 2)
    duration = scenes[index + 1][0] - at if index + 1 < len(scenes) else .05
    state = shot_state(visible, camera, aperture, atmosphere)
    tracks = {address: [(0, value, "hold")] for address, value in state.items()}
    score(index, tracks)
    clips.append(dict(id=id, name=title, duration=duration, loop="once", tracks=track_list(tracks)))
    for address, keys in tracks.items():
        target = reference.setdefault(address, [])
        # Explicitly hold the previous shot until the next cut. Incoming eased keys
        # can move within a shot but must never interpolate across a cue boundary.
        if target and at > 0 and address[1] != "enable":
            target.append((at - .001, max(target, key=lambda key: key[0])[1], "hold"))
        target.extend((round(at + time, 3), value, curve) for time, value, curve in keys)

    values = [dict(layer="0", path=f"control/{key}", value=value)
              for (key, _, _), value in zip(scene_controls, mix)]
    description = f"{at // 60:02d}:{at % 60:02d} · {title} — {gesture}"
    presets.append(dict(id=id, name=title, fade=0, values=values))
    automatic.append(dict(preset=id, description=description, pre_wait=at))
    manual.append(dict(description=description, actions=[
        *(dict(timeline=str(clip_id), action="stop") for clip_id in range(1, len(scenes) + 2)),
        dict(preset=id), dict(timeline=id, action="play"),
    ]))

    if index == 0:
        # Match the first cue when the project opens and GO is still on standby.
        by_id = {item["id"]: item for item in layers}
        for (target, param), value in state.items():
            if param == "enable":
                by_id[target]["enable"] = bool(value)
            else:
                by_id[target]["params"][param] = value

timelines = [dict(id="1", name="Camera Inversa · montage", duration=60, loop="once",
                  tracks=track_list(reference)), *clips]
automatic = [
    *(dict(timeline=str(clip_id), action="stop") for clip_id in range(1, len(scenes) + 2)),
    automatic[0], dict(timeline="1", action="play", description="Start the locked picture and sound score"),
    *automatic[1:],
]
project = dict(app="NyxMap", format_version=1,
               composition=dict(width=1280, height=720, fps=60, speed=1, quality_3d=[3, 2, 2, 2, 2, 2]),
               audio=dict(volume=.72), layers=layers, materials=list(materials.values()),
               controls=controls, interactions=interactions,
               timelines=timelines, presets=presets, sequences=[
                   dict(id="1", name="Camera Inversa · Live GO", loop=False, steps=manual),
                   dict(id="2", name="Camera Inversa · Reference (1:00)", loop=False,
                        steps=[dict(description="Play the one-minute score", actions=automatic)]),
               ], current_sequence=1, startup_sequence=1, startup_step=0, startup_go=False,
               ui=dict(selected_layer=0))
write_json(ROOT / "Camera Inversa.nyxmap", project)
