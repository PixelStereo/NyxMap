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
    return dict(id=id, name=name, enable=True, locked=False, viewports={}, fx=[],
                play=True, source=media, params=params, audio_fx=list(audio_fx))


def effect(id, kind, wet, **values):
    return dict(id=id, kind=kind, enabled=True, wet=wet, values=values)


def voice(id, key, name, kind, effects=(), **recipe):
    filename = f"{key}.nyxaudio"
    write_json(AUDIO / filename, dict(version=1, type=kind, **recipe))
    return layer(id, name, source("audio", f"audio/{filename}"),
                 {"play": True, "play_mode": "loop", "volume": 1 if key == "strike" else 0, "audio_fx/enable": True}, effects)


IMAGE, AIR, METAL, SUB, STRIKE = "101", "102", "103", "104", "105"
bowed_metal()
layers = [
    layer(IMAGE, "Volumetric chamber", source("isf", "Camera Inversa.fs", width=960, height=540),
          {"opacity": 0}),
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
]

# Every scene has a playable mix. The last five controls are independent of scene recall.
scene_controls = [
    ("image", "01 · Image", 0, [(IMAGE, "opacity", 0, 1)]),
    ("smoke", "02 · Smoke density", .2, [(IMAGE, "param/smoke", 0, 1)]),
    ("walls", "03 · Wall reveal", .08, [(IMAGE, "param/architecture", 0, 1)]),
    ("depth", "04 · Relief ↔ cavity", 0, [(IMAGE, "param/inversion", 0, 1)]),
    ("rgb", "05 · RGB memory", 0, [(IMAGE, "param/glitch", 0, .95)]),
    ("air", "06 · Air level", 0, [(AIR, "volume", 0, .7)]),
    ("metal", "07 · Granular level", 0, [(METAL, "volume", 0, .75)]),
    ("sub", "08 · Sub level", 0, [(SUB, "volume", 0, .9)]),
    ("density", "09 · Grain density", .16, [(METAL, "synth/density", 2, 72)]),
    ("size", "10 · Grain duration", .5, [(METAL, "synth/grainMs", 18, 265)]),
]
live_controls = [
    ("camera", "11 · Camera angle", .52, [(IMAGE, "param/view", 0, 1)]),
    ("drift", "12 · Smoke turbulence", .37, [(IMAGE, "param/drift", 0, 1)]),
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

# The mix drives secondary visual parameters, leaving the performance faders free.
for lo, hi, param, release in [(32, 106, "pressure", .43), (1000, 5400, "scatter", .23)]:
    route(f"{lo}–{hi} Hz transients → {param}", IMAGE, f"param/{param}",
          [dict(type="envelope", a=.025, b=release, c=0, d=.025)],
          source="audio", audio_low=lo, audio_high=hi, audio_feature=3,
          audio_input=False, audio_sensitivity=.64, audio_gap=.09)

# A free-running, deliberately uneven sub phrase. Scene presets never touch its gate or pitch.
pulses = [(1.1, 1.36, 42, .83), (3.4, .31, 49, .67), (4.02, .72, 43, .9),
          (7.15, 1.6, 37, .88), (10.2, .25, 54, .64), (10.74, .45, 46, .82),
          (13.5, 2.2, 40, .97), (17.8, .8, 43, .9)]
timeline = dict(id="1", name="Pressure · 21 s", duration=21, loop="loop", tracks=[
    dict(layer=SUB, param="synth/gate", keys=[[0, 0, "hold"]] + [
        key for start, length, hz, velocity in pulses
        for key in ([start, velocity, "hold"], [round(start + length, 3), 0, "hold"])
    ]),
    dict(layer=SUB, param="synth/frequency", keys=[[0, 42, "hold"]] + [
        [start, hz, "hold"] for start, length, hz, velocity in pulses
    ]),
])

# The last column suggests a gesture. It is visible beside the cue on Show Control.
# Values follow scene_controls in order.
scenes = [
    (0, "First breath",       (1,.23,.02,0,0,.36,.02,0,.05,.73), "Raise smoke until the first filaments read."),
    (16, "Silver thread",     (1,.38,.11,0,0,.31,.14,0,.11,.82), "Move the camera slowly across the first edge."),
    (32, "Solid edge",        (1,.43,.72,.04,0,.21,.22,.36,.14,.56), "Weight the sub to bring the wall forward."),
    (49, "Compression",      (1,.52,.94,.08,.04,.12,.22,.73,.27,.38), "Hold the edge. Shorten grains for sharper attacks."),
    (65, "Fault line",       (1,.47,.91,.13,.19,.15,.45,.45,.44,.32), "Send a metal strike into the RGB memory."),
    (74, "Channel shear",    (1,.54,.98,.2,.57,.11,.61,.3,.68,.19), "Push RGB or leave the tear at the edges."),
    (92, "Grain storm",      (1,.72,.88,.29,.8,.29,.76,.15,.87,.1), "Play grain size against density."),
    (110, "Pressure drop",   (1,.43,.95,.35,.27,.12,.19,0,.2,.48), "Let the gap open. Keep the lip in frame."),
    (116, "Hinge",           (1,.57,1,.44,.08,.35,.09,0,.09,.72), "Sweep depth slowly through the flat plane."),
    (124, "Camera Inversa",  (1,.75,1,1,.11,.41,.21,.26,.19,.8), "Watch the same edge become the lip of a cavity."),
    (141, "Deep chamber",    (1,.83,.92,.97,.04,.27,.43,.58,.32,.71), "Open the room and lean into the sub."),
    (158, "Residue",         (1,.36,.35,.96,0,.2,.31,.05,.12,.88), "Pull back air and leave a few long grains."),
    (173, "Afterimage",      (.5,.18,.08,1,0,.08,.19,0,.04,.9), "Leave the last filament suspended."),
    (177, "Black / tail",    (0,0,0,1,0,0,.13,0,.03,.93), "Listen on black."),
    (180, "End",             (0,0,0,0,0,0,0,0,0,.5), "Silence and stop the pressure phrase."),
]
presets, manual, automatic = [], [], []
for index, (at, title, values, gesture) in enumerate(scenes):
    id = str(index + 1)
    presets.append(dict(id=id, name=title, fade=0, values=[
        dict(layer="0", path=f"control/{key}", value=value)
        for (key, _, _, _), value in zip(scene_controls, values)
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
automatic.insert(1, dict(timeline="1", action="play", description="Start pressure phrase"))
automatic.append(dict(timeline="1", action="stop", pre_wait=180, description="Stop pressure phrase"))

project = dict(app="NyxMap", format_version=1,
               composition=dict(width=1280, height=720, fps=60, speed=1),
               audio=dict(volume=.7), layers=layers, controls=controls,
               interactions=interactions, timelines=[timeline], presets=presets,
               sequences=[
                   dict(id="1", name="Camera Inversa · Live GO", loop=False, steps=manual),
                   dict(id="2", name="Camera Inversa · 3:00", loop=False, steps=[dict(description="Play the three-minute score", actions=automatic)]),
               ],
               current_sequence=0, startup_sequence=0, startup_step=0, startup_go=False,
               ui=dict(selected_layer=0))
write_json(ROOT / "Camera Inversa.nyxmap", project)
