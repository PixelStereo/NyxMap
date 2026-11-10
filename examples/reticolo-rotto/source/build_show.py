#!/usr/bin/env python3
"""Score one Beam Lattice ISF against an original break bass track."""

from collections import defaultdict
import json
from pathlib import Path
import sys

from build_sound import AUDIO, BAR, BARS, SOUND, STEP, sound_tracks

ROOT = Path(__file__).resolve().parents[1]
BEAM = "101"
W, H = 1280, 720
TOTAL = BARS * BAR
SHADER = "Beam Lattice.fs"

# Angles, colors, and the beam families change with the music's phrasing.
# Crop and corner pins are native spatial mapping, not additional passes.
SHOTS = [
    (0, "Fessura", "A thin icy slit; distant percussion behind it.",
     (.42, .18, .10, .35, 0, 0, -4, 0, 39, .81, (.49,.76,1), 100, 27, .25)),
    (4, "Impatto", "The first break tears the frame open into orange steel.",
     (.94, .18, .38, .16, 0, 9, 3, -12, 100, .39, (1,.39,.19), 100, 100, 0)),
    (10, "Taglio", "Half-time. A diagonal crop rides the ghosts of the snare.",
     (.66, .63, .22, .66, 1, 12, -5, 38, 54, .69, (.31,.99,.88), 92, 44, -13)),
    (14, "Multipli", "Tight pitch turns the bars into a violet field of repeats.",
     (.86, .20, .86, .12, 2, -6, 3, 16, 117, .42, (.81,.46,1), 100, 100, 0)),
    (20, "Mappa", "The room folds into an off-axis panel, with the break stripped away.",
     (.53, .28, .43, .94, 1, -25, 9, -19, 78, .69, (.65,.88,1), 72, 68, -8)),
    (24, "Controcolpo", "The second drop runs backward through red hot crossbeams.",
     (1, .05, .52, .42, 0, -13, -8, -26, 112, .35, (1,.19,.33), 100, 100, 0)),
    (30, "Residuo", "One last kick, then the aperture closes to black.",
     (.47, .33, .31, .58, 1, 2, -12, 6, 43, .95, (.63,.78,1), 100, 27, 0)),
]

controls, interactions = [], []


def route(name, target, filters, layer=BEAM, **origin):
    id = str(len(interactions) + 1)
    interactions.append(dict(id=id, name=name, enabled=True, layer=layer, param=target, master=1,
                             takeover="jump", filters=filters, **origin))
    return id


def remap(low, high):
    return dict(type="remap", a=0, b=1, c=low, d=high)


def audio_hit(name, lo, hi, target, floor, peak, release):
    return route(name, target, [dict(type="attack_release", a=.005, b=release), remap(floor, peak)],
                 source="audio", audio_feature=3, audio_low=lo, audio_high=hi, audio_input=False,
                 audio_sensitivity=.76, audio_gap=.065)


kick = audio_hit("Kick → beam impact", 34, 122, "param/hit", 0, .78, .105)
snare = audio_hit("Snare → light bloom", 420, 4200, "param/glow", .54, 2.45, .13)
hats = audio_hit("Metal → beam edge", 6500, 14500, "param/thickness", .019, .034, .075)


def control(key, name, value, *targets):
    controls.append(dict(id=str(len(controls)+1), key=key, name=name, kind="slider",
                         min=0, max=1, step=.01, default=value, value=value))
    for layer, path, low, high in targets:
        route(f"{name} → {path}", path, [remap(low, high)], layer=layer, source="internal",
              source_layer="0", source_param=f"control/{key}")


impact, crop, density, mapping = SHOTS[0][3][:4]
control("impact", "01 · Impact", impact, ("0", f"interaction/{kick}/master", 0, 1))
control("crop", "02 · Crop", crop, (BEAM, "roi/left", 0, .38), (BEAM, "roi/right", 1, .62))
control("multipli", "03 · Multipli", density, (BEAM, "param/cell", 3.6, .72))
control("mapping", "04 · FX Mapping", mapping,
        (BEAM, "corner/top_left/x", 0, 28), (BEAM, "corner/top_left/y", 0, 12),
        (BEAM, "corner/top_right/y", 0, -15), (BEAM, "corner/bottom_right/x", 0, -18),
        (BEAM, "corner/bottom_left/x", 0, -9), (BEAM, "corner/bottom_left/y", 0, -12))
control("bloom", "05 · Bloom", .85, ("0", f"interaction/{snare}/master", .38, 1))
control("twist", "06 · Twist", .45, (BEAM, "param/warp", -1.16, 1.38))
control("edge", "07 · Edge", 1, ("0", f"interaction/{hats}/master", .60, 1))
control("kick_level", "08 · Kick", .82, (AUDIO, "synth/kick_level", 0, 1))
control("bass_level", "09 · Bass", .78, (AUDIO, "synth/bass_level", 0, 1))
control("bass_drive", "10 · Bass Drive", .65, (AUDIO, "synth/bass_drive", 0, 1))
control("width", "11 · Width", .68, (AUDIO, "synth/width", 0, 1))
control("break_level", "12 · Break", .74, (AUDIO, "synth/break_level", 0, 1))


def frame(shot, number):
    _, _, _, _, axes, yaw, pitch, roll, fov, fog, color, sx, sy, angle = shot[3]
    speeds = (.24, 1.13, .37, .79, .16, -1.19, -.26)
    values = dict(opacity=1, rotation=angle)
    values.update({f"param/{p}": v for p, v in dict(axes=axes, yaw=yaw, pitch=pitch, roll=roll, fov=fov,
                   fog=fog, speed=speeds[number]).items()})
    values.update({f"param/tint/{c}": v for c, v in zip("rgb", color)})
    values.update({"scale/x": sx, "scale/y": sy, "roi/top": (100-sy)/300, "roi/bottom": 1-(100-sy)/300,
                   "position/x": W*(.55 if number == 4 else .5), "position/y": H*.5})
    return values


def layer(id, name, source, params):
    return dict(id=id, name=name, enable=True, locked=False, viewports={}, fx=[],
                source=source, params=params)


opening = frame(SHOTS[0], 0)
layers = [
    layer(BEAM, "Beam Lattice", dict(type="isf", path=SHADER,
          relative_path=f"../../isf/generators/{SHADER}", width=W, height=H),
          dict(opening, **{"param/thickness": .019, "param/glow": .52})),
    layer(AUDIO, "Reticolo Rotto · Live synth", dict(type="audio", path=SOUND, relative_path=SOUND),
          {"play": True, "volume": 1.0}),
]


def score(number, start, end):
    shot = SHOTS[number]
    values = frame(shot, number)
    duration = end - start
    moves = defaultdict(dict)
    events = defaultdict(dict)

    def key(param, time, value, out="hold"):
        moves[(BEAM, param)][round(time, 5)] = (value, out)

    def event(layer, param, time, value):
        events[(layer, param)][round(time, 5)] = value

    for param, value in values.items():
        if param == "param/axes":
            event(BEAM, param, 0, value)
        else:
            key(param, 0, value)

    def glide(param, *points):
        key(param, 0, values[param], 3)
        for i, (t, value) in enumerate(points):
            key(param, t, value, 3 if i < len(points)-1 else "hold")

    if number == 0:
        glide("scale/y", (duration*.83, 13), (duration-.28, 29))
        glide("param/fov", (duration*.66, 65), (duration-.19, 43))
    if number in (1, 2, 3, 5):
        # Sync framing to the same backbeat as the original drums, leaving the light to listen live.
        for bar in range(round(duration/BAR)):
            base = bar*BAR
            for beat in ((8,) if number == 2 else (4, 12)):
                if bar == round(duration/BAR)-1 and beat >= 10:
                    continue
                time = base + beat*STEP
                size = values["scale/y"]
                key("scale/y", time, size*(.58 if number == 2 else .78), 3)
                key("scale/y", time + STEP*(2.6 if number == 2 else 1.7), size)
                key("param/roll", time, values["param/roll"]+(10 if bar%2 else -9), 3)
                key("param/roll", time + STEP*2.15, values["param/roll"])
        for bar in range(round(duration/BAR)):
            at = bar*BAR
            key("param/yaw", at, values["param/yaw"] + (0, 8, -9)[bar%3], 3)
        key("param/yaw", duration-.23, values["param/yaw"])
        if number in (3, 5):
            event(BEAM, "param/axes", duration*.5, 0 if number == 3 else 2)
            event(BEAM, "param/axes", duration*.5+BAR, values["param/axes"])
        if number in (1, 3, 5):
            key("opacity", duration - STEP*2, 0)
    if number == 4:
        glide("position/x", (duration*.39, W*.36), (duration*.8, W*.57))
        glide("rotation", (duration*.5, 17), (duration-.19, -22))
        glide("scale/x", (duration*.67, 47), (duration-.2, 82))
        glide("param/fov", (duration*.66, 31), (duration-.23, 122))
    if number == 6:
        glide("scale/y", (duration-.3, 1))
        key("opacity", .92, 1, 3)
        key("opacity", duration-.14, 0)
    return moves, events


def tracks(moves, events):
    return [dict(layer=id, param=p, keys=[[t, v, c] for t, (v,c) in sorted(keys.items())])
            for (id,p), keys in moves.items()] + [
        dict(layer=id, param=p, type="event", keys=sorted(keys), values=[keys[t] for t in sorted(keys)])
        for (id,p), keys in events.items()]


all_moves, all_events = defaultdict(dict), defaultdict(dict)
presets, clips, live = [], [], []
ids = [str(i+2) for i in range(len(SHOTS))]
for number, ((bar, title, description, _), id) in enumerate(zip(SHOTS, ids)):
    start = bar*BAR
    end = SHOTS[number+1][0]*BAR if number+1 < len(SHOTS) else TOTAL
    moves, events = score(number, start, end)
    moves.update(sound_tracks(bar, round(end/BAR)))
    for src, dst in ((moves, all_moves), (events, all_events)):
        for k, items in src.items():
            dst[k].update({round(start+t, 5): value for t, value in items.items()})
    clips.append(dict(id=id, name=title, duration=end-start, loop="once", tracks=tracks(moves, events)))
    presets.append(dict(id=id, name=title, fade=0, values=[
        dict(layer="0", path=f"control/{k}", value=v)
        for k, v in zip(("impact", "crop", "multipli", "mapping"), SHOTS[number][3])
    ]))
    live.append(dict(text=title, description=description, actions=[
        *[dict(timeline=other, action="stop") for other in ["1", *ids]],
        dict(preset=id), dict(timeline=id, action="play"),
    ]))

reference = dict(text="GO", description="Run the full 48-second audiovisual score.", actions=[
    *[dict(timeline=id, action="stop") for id in ["1", *ids]],
    dict(preset=ids[0]), dict(timeline="1", action="play"),
    *[dict(preset=id, pre_wait=shot[0]*BAR) for id, shot in zip(ids[1:], SHOTS[1:])],
])
project = dict(app="NyxMap", format_version=1, composition=dict(width=W, height=H, fps=60, speed=1),
               audio=dict(volume=.88), layers=layers, controls=controls, interactions=interactions,
               timelines=[dict(id="1", name="Reticolo Rotto · Score", duration=TOTAL, loop="once",
                               tracks=tracks(all_moves, all_events)), *clips],
               presets=presets, snapshots=[], sequences=[
                   dict(id="1", name="Reticolo Rotto · Reference", loop=False, steps=[reference]),
                   dict(id="2", name="Reticolo Rotto · Live GO", loop=False, steps=live),
               ], current_sequence=0, startup_sequence=0, startup_step=0,
               startup_go="--autoplay" in sys.argv, ui=dict(selected_layer=0))
(ROOT / "Reticolo Rotto.nyxmap").write_text(json.dumps(project, ensure_ascii=False, separators=(",", ":")) + "\n",
                                          encoding="utf-8")
