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


WORLD, STAGE, SMOKE, FRAME, SHARDS = "101", "107", "108", "109", "110"
visuals = [
    dict(layer(WORLD, "Camera Inversa · image", dict(type="none"), {
        "opacity": 0, "fx/enable": True,
    }), group=True, collapsed=True),
    dict(layer(SMOKE, "Smoke", dict(type="isf", width=960, height=540, **library_shader("smoke.fs")),
               {"blend_mode": "add", "opacity": .8}), parent=WORLD),
    dict(layer(STAGE, "Chamber · 3D", dict(type="none"), {
        "opacity": 0, "camera/distance": 9, "camera/fov": 49,
        "camera/pitch": 6, "camera/yaw": -7, "camera/target/z": -1.8,
        "scene/studio": 0, "scene/ambient_intensity": .10, "scene/exposure": .92,
        **axes("scene/background", "rgba", (0, 0, 0, 1)),
        "scene/glow": .68, "scene/glow_threshold": .52, "scene/vignette": .31,
        "scene/fog/density": .075, "scene/fog/reach": 29, "scene/fog/falloff": .05,
        **axes("scene/fog/color", "rgb", (.045, .045, .045)),
    }), parent=WORLD, group=True, scene3d=True, collapsed=True),
    dict(layer(FRAME, "Inverting frame", dict(type="none"), {}),
         parent=STAGE, group=True, merge3d=True, collapsed=True),
]
visuals[0]["fx"] = [library_shader(name, "effects") for name in ("Pixelate.fs",)]


def model(name, parent, shape, position, scale, gray, rotation=(0, 0, 0), unlit=True):
    id = str(200 + len(visuals))
    visuals.append(dict(layer(id, name, dict(type="model", path=f"/NyxMap/3D/{shape}"), {
        "model/fit": True, "model/detail": 1, "model/unlit": unlit,
        "model/material/metal": .42, "model/material/rough": .38,
        **axes("model/position", "xyz", position),
        **axes("model/rotation", "xyz", rotation),
        **axes("model/scale", "xyz", scale),
        **axes("model/material/color", "rgb", (gray,) * 3),
    }), parent=parent))
    return id


ribs = []
for k in range(7):
    rib = str(300 + k)
    ribs.append(rib)
    visuals.append(dict(layer(rib, f"Rib {k + 1}", dict(type="none"), {
        "merge/position/z": -k * .48,
        "merge/scale/all": 1 + k * .055,
    }), parent=FRAME, group=True, merge3d=True, collapsed=True))
    gray = .78 - .085 * k
    for side in (-1, 1):
        model("Lintel", rib, "Cube", (0, side * 1.26, 0), (4.38, .035, .075), gray)
        model("Jamb", rib, "Cube", (side * 2.17, 0, 0), (.035, 2.52, .075), gray)

visuals.append(dict(layer(SHARDS, "Fracture field", dict(type="none"), {}),
                    parent=STAGE, group=True, merge3d=True, collapsed=True, enable=False))
rng = random.Random(417)
for k in range(24):
    angle = k * 2.39996
    radius = rng.uniform(.8, 3.1)
    model(f"Splinter {k + 1}", SHARDS, "Cube",
          (math.cos(angle) * radius, math.sin(angle) * radius * .66, rng.uniform(-5, 1.5)),
          (rng.uniform(.4, 2.1), rng.uniform(.012, .06), .04),
          rng.uniform(.12, .72), (rng.uniform(-65, 65), rng.uniform(-50, 50), rng.uniform(-90, 90)))

model("Black floor", STAGE, "Plane", (0, -1.55, -3), (19, 19, 1), .045, (-90, 0, 0), False)
for id, name, position, aim in [
    ("111", "Left searchlight", (-3.2, 2.4, 2), (1.2, -.85, -4.6)),
    ("112", "Right searchlight", (3.2, -.7, -1.4), (-1, 1.8, -5.6)),
]:
    visuals.append(dict(layer(id, name, dict(type="light"), {
        "light/type": "spot", "light/lookat": True, "light/angle": 11,
        "light/intensity": 40, "light/rays": 1.5, "light/shadow": False,
        **axes("light/position", "xyz", position),
        **axes("light/lookpoint", "xyz", aim),
    }), parent=STAGE))

AIR, METAL, SUB, STRIKE, BREAK = "102", "103", "104", "105", "106"
bowed_metal()
layers = visuals + [
    voice(AIR, "air", "Friction air", "pink",
          [effect("air-filter", "bandpass", 1, cutoff=960, resonance=1.4)],
          gain=.16, attack=.8, sustain=1, release=1.4, gate=1),
    voice(METAL, "metal", "Bowed metal grains", "granular",
          [effect("metal-room", "reverb", .3, room=.83, damping=.52)],
          source="bowed-metal.wav", grainMs=110, density=11, pitch=1, gain=.24,
          attack=.15, sustain=1, release=.45, gate=1),
    voice(SUB, "sub", "Asymmetric pressure", "fm",
          [effect("sub-filter", "lowpass", 1, cutoff=230, resonance=.8)],
          frequency=43, ratio=1.005, index=.5, gain=.23,
          attack=.06, decay=.32, sustain=.73, release=.42, gate=0),
    voice(STRIKE, "strike", "Hand-triggered metal", "fm",
          [effect("strike-room", "reverb", .36, room=.8, damping=.45)],
          frequency=270, ratio=2.41, index=4.6, gain=.13,
          attack=.006, decay=.29, sustain=.12, release=.38, gate=0),
    voice(BREAK, "break", "Broken contact noise", "white",
          [effect("break-band", "bandpass", 1, cutoff=1800, resonance=1.9),
           effect("break-drive", "distortion", .64, drive=.46, tone=3000)],
          gain=.27, attack=.002, decay=.053, sustain=.08, release=.07, gate=0),
]

# Every scene has a playable mix. The last five controls are independent of scene recall.
scene_controls = [
    ("image", "01 · Image", 0, [(WORLD, "opacity", 0, 1)]),
    ("smoke", "02 · Smoke density", .2, [(SMOKE, "param/densityAmount", 0, 1.45)]),
    ("walls", "03 · Wall reveal", .08, [(STAGE, "opacity", 0, 1)]),
    ("depth", "04 · Relief ↔ cavity", 0, [(rib, "merge/position/z", k * .42, -k * .88) for k, rib in enumerate(ribs)]),
    ("air", "05 · Air level", 0, [(AIR, "volume", 0, .7)]),
    ("metal", "06 · Granular level", 0, [(METAL, "volume", 0, .75)]),
    ("sub", "07 · Sub level", 0, [(SUB, "volume", 0, .9)]),
    ("density", "08 · Grain density", .16, [(METAL, "synth/density", 2, 72)]),
    ("size", "09 · Grain duration", .5, [(METAL, "synth/grainMs", 18, 265)]),
    ("fracture", "10 · Fracture / contact noise", 0, [
        (BREAK, "volume", 0, 1),
        (SHARDS, "enable", 0, 5),
        (WORLD, "fx/Pixelate/enable", 0, .75),
        (WORLD, "fx/Pixelate/param/gap", 0, .13),
    ]),
]
live_controls = [
    ("camera", "11 · Camera angle", .52, [(STAGE, "camera/yaw", -49, 39)]),
    ("drift", "12 · Smoke turbulence", .37, [(SMOKE, "param/turbulence", 0, 1)]),
    ("pitch", "13 · Grain transposition", .48, [(METAL, "synth/pitch", .35, 1.7)]),
    ("room", "14 · Grain space", .3, [(METAL, "audio_fx/metal-room/wet", 0, .8)]),
    ("strike", "15 · Strike / hold", 0, [(STRIKE, "synth/gate", 0, 1)]),
]
controls, interactions = [], []


def route(name, target, param, filters, **origin):
    interactions.append(dict(id=str(len(interactions) + 1), name=name, enabled=True,
                             layer=target, param=param, master=1, takeover="jump",
                             filters=filters, **origin))


for key, name, initial, targets in scene_controls + live_controls:
    kind = "button" if key == "strike" else "slider"
    controls.append(dict(id=str(len(controls) + 1), key=key, name=name, kind=kind,
                         min=0, max=1, step=.01, default=initial, value=initial))
    for target, param, low, high in targets:
        route(name, target, param, [dict(type="remap", a=0, b=1, c=low, d=high)],
              source="internal", source_layer="0", source_param=f"control/{key}")

# Keep audio reactions on secondary parameters so the live faders stay independent.
for lo, hi, target, param, low, high, release in [
    (32, 106, STAGE, "camera/distance", 9.4, 7.6, .43),
    (32, 106, "111", "light/intensity", 45, 410, .34),
    (850, 5400, "112", "light/intensity", 25, 285, .19),
    (850, 5400, SHARDS, "merge/scale/all", .83, 1.36, .17),
]:
    route(f"{lo}–{hi} Hz transients → {param}", target, param,
          [dict(type="envelope", a=.022, b=release, c=0, d=.025),
           dict(type="remap", a=0, b=1, c=low, d=high)],
          source="audio", audio_low=lo, audio_high=hi, audio_feature=3,
          audio_input=False, audio_sensitivity=.64, audio_gap=.09)

# A free-running, deliberately uneven sub phrase. Scene presets never touch its gate or pitch.
pulses = [(1.1, 1.36, 42, .83), (3.4, .31, 49, .67), (4.02, .72, 43, .9),
          (7.15, 1.6, 37, .88), (10.2, .25, 54, .64), (10.74, .45, 46, .82),
          (13.5, 2.2, 40, .97), (17.8, .8, 43, .9)]
timeline = dict(id="1", name="Pressure and fracture · 21 s", duration=21, loop="loop", tracks=[
    dict(layer=SUB, param="synth/gate", keys=[[0, 0, "hold"]] + [
        key for start, length, hz, velocity in pulses
        for key in ([start, velocity, "hold"], [round(start + length, 3), 0, "hold"])
    ]),
    dict(layer=SUB, param="synth/frequency", keys=[[0, 42, "hold"]] + [
        [start, hz, "hold"] for start, length, hz, velocity in pulses
    ]),
])

# The contact mic and 3D field share an asymmetrical burst clock.
burst_times = [0.76, .93, 1.61, 3.7, 3.83, 3.94, 5.9, 8.31, 8.58, 8.71,
               10.8, 12.29, 12.41, 12.61, 12.9, 16.44, 18.7, 18.77, 19.12]
for target, param, initial, keys in [
    (BREAK, "synth/gate", 0, [point for t in burst_times for point in
                              ([t, rng.uniform(.32, 1), "hold"], [round(t+.037, 3), 0, "hold"])]),
    (BREAK, "audio_fx/break-band/cutoff", 1800,
     [[t, rng.choice((640, 910, 1420, 2700, 4200)), "hold"] for t in burst_times]),
    (SHARDS, "merge/rotation/y", 0, [[t, rng.uniform(-56, 56), "hold"] for t in burst_times]),
    (SHARDS, "merge/rotation/z", 0, [[t, rng.uniform(-27, 27), "hold"] for t in burst_times]),
    (WORLD, "fx/Pixelate/param/cellSize", 8, [[t, rng.choice((4, 12, 26, 52)), "hold"] for t in burst_times]),
]:
    timeline["tracks"].append(dict(layer=target, param=param, keys=[[0, initial, "hold"], *keys]))

# Cues carry both a mix and a shot. Shot values avoid the addresses driven by
# live controls, audio interactions and the irregular fracture clock.
# Shot: field of view, pitch, aim XYZ, frame rotation, rib width, fog,
# smoke opacity and spread, air filter frequency, left/right light levels.
scenes = [
    (0, 'First breath',
     (1, 0.32, 0, 0, 0.31, 0, 0, 0.05, 0.73, 0),
     (53, 0, (0, 0, 0), 0, 1, 0, 0.55, 0.43, 460, (0, 0)),
     'A thin plume emerges in the center. Leave space around the breath.'),
    (8, 'Silver thread',
     (1, 0.25, 0.68, 0, 0.13, 0.24, 0, 0.12, 0.86, 0),
     (19, 9, (-1.9, 0.93, 0.4), -8, 0.75, 0.018, 0.28, 0.55, 860, (0.25, 0)),
     'A tight crop catches the top corner. Let one long grain hang on the edge.'),
    (16, 'Solid edge',
     (1, 0.2, 1, 0.01, 0.1, 0.16, 0.53, 0.1, 0.58, 0),
     (44, 18, (0, 0.1, 0), 12, 1.08, 0.026, 0.36, 0.73, 540, (0.65, 0.14)),
     'Cut wide. The ribs stand in relief and the first sub pulses enter.'),
    (25, 'Grain storm',
     (1, 0.26, 1, 0.52, 0.2, 0.78, 0.12, 0.88, 0.1, 0.97),
     (61, 44, (0, 0, -2), 71, 0.54, 0.084, 0.34, 1.24, 3710, (1, 1)),
     'A wide burst of splinters and short grains.'),
    (34, 'Pressure drop',
     (0, 0, 0, 0.52, 0, 0, 0, 0.09, 0.72, 0),
     (42, 0, (0, 0, 0), 0, 1, 0, 0, 1, 460, (0, 0)),
     'Cut to black and silence.'),
    (37, 'Camera Inversa',
     (1, 0.32, 1, 1, 0.22, 0.2, 0.34, 0.18, 0.77, 0),
     (55, -6, (0, 0, -3.8), 0, 1, 0.035, 0.37, 0.77, 610, (0.46, 0.46)),
     'Look straight down the cavity as the ribs fall away from the lip.'),
    (50, 'Residue',
     (1, 0.39, 0, 1, 0.11, 0.26, 0, 0.12, 0.89, 0),
     (48, 4, (0, 0, -2), 0, 1, 0.02, 0.5, 0.48, 940, (0, 0)),
     'Lose the chamber. Leave the dissolving plume and a few long grains.'),
    (57, 'Black / tail',
     (0, 0, 0, 1, 0, 0.13, 0, 0.03, 0.93, 0),
     (48, 4, (0, 0, -2), 0, 1, 0, 0, 0.36, 620, (0, 0)),
     'Listen on black.'),
    (60, 'End',
     (0, 0, 0, 0, 0, 0, 0, 0, 0.5, 0),
     (48, 4, (0, 0, -2), 0, 1, 0, 0, 0.36, 620, (0, 0)),
     'Silence and stop the pressure and fracture clock.'),
]

def shot_values(shot):
    fov, pitch, target, roll, width, fog, smoke, spread, air_band, lights = shot
    return [
        (STAGE, "camera/fov", fov), (STAGE, "camera/pitch", pitch),
        *((STAGE, f"camera/target/{axis}", value) for axis, value in zip("xyz", target)),
        (FRAME, "merge/rotation/z", roll), (FRAME, "merge/scale/x", width),
        (STAGE, "scene/fog/density", fog), (SMOKE, "opacity", smoke),
        (SMOKE, "param/spread", spread), (AIR, "audio_fx/air-filter/cutoff", air_band),
        ("111", "light/level", lights[0]), ("112", "light/level", lights[1]),
    ]


presets, manual, automatic = [], [], []
for index, (at, title, mix, shot, gesture) in enumerate(scenes):
    id = str(index + 1)
    values = [("0", f"control/{key}", value)
              for (key, _, _, _), value in zip(scene_controls, mix)]
    values.extend(shot_values(shot))
    presets.append(dict(id=id, name=title, fade=0, values=[
        dict(layer=layer_id, path=path, value=value) for layer_id, path, value in values
    ]))
    description = f"{at // 60:02d}:{at % 60:02d} · {title} — {gesture}"
    step = dict(preset=id, description=description)
    if index in (0, len(scenes) - 1):
        # Both transports start and stop with their corresponding scene.
        step = dict(description=description, actions=[
            dict(preset=id),
            dict(timeline="1", action="play" if index == 0 else "stop"),
        ])
    manual.append(step)
    automatic.append(dict(preset=id, description=description, pre_wait=at))

# A single clocked group fires each scene relative to GO. The looping child
# can overlap the scene recalls without holding up their dispatch.
automatic.insert(1, dict(timeline="1", action="play", description="Start pressure and fracture clock"))
automatic.append(dict(timeline="1", action="stop", pre_wait=60, description="Stop pressure and fracture clock"))

project = dict(app="NyxMap", format_version=1,
               composition=dict(width=1280, height=720, fps=60, speed=1),
               audio=dict(volume=.7), layers=layers, controls=controls,
               interactions=interactions, timelines=[timeline], presets=presets,
               sequences=[
                   dict(id="1", name="Camera Inversa · Live GO", loop=False, steps=manual),
                   dict(id="2", name="Camera Inversa · Reference (1:00)", loop=False, steps=[dict(description="Play the one-minute score", actions=automatic)]),
               ],
               current_sequence=1, startup_sequence=1, startup_step=0, startup_go=False,
               ui=dict(selected_layer=0))
write_json(ROOT / "Camera Inversa.nyxmap", project)
