#!/usr/bin/env python3
"""Build Demo: a 30-second audiovisual piece played by its soundtrack's timecode.

The soundtrack is not in the repository: give its file with --audio (any 30 s track works; the cues are placed on the
beat of the one it was written for, 78 BPM). The show folder gets the project, its shaders (copied from NyxMap's ISF
library) and the soundtrack.

    python3 examples/demo/build_show.py --audio track.mp3 --out ~/Shows/Demo

Open Demo.nyxmap: the sequence stands by on its first step; GO starts the soundtrack, and from there the
soundtrack's playhead plays every other step at its timecode.
"""
import argparse
import copy
import json
import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent
ISF = HERE.parents[1] / "isf"
W, H = 1920, 1080

# The beat of the soundtrack (78 BPM): beat n at OFFSET + n × BEAT seconds, a bar every 4 beats
BEAT, OFFSET = 0.7694, 0.04


def beat(n):
    return round(OFFSET + n * BEAT, 3)


# ---------------------------------------------------------------------------------------------------------------------
# Layers, top first (the first one is drawn over the others)
# ---------------------------------------------------------------------------------------------------------------------
GENERATORS = {"data": "generators/Data Stream.fs", "lattice": "generators/Beam Lattice.fs",
              "bars": "generators/Light Bars.fs", "waves": "generators/Waveform Lines.fs",
              "wash": "generators/Linear Gradient.fs"}
EFFECTS = {"crop": "effects/Soft Crop.fs", "kaleido": "effects/Kaleidoscope.fs", "zoom": "effects/Zoom Volumetric.fs",
           "vhs": "effects/VHS Glitch.fs", "trails": "effects/Trails.fs"}
FX_SEGMENT = {"crop": "Soft_Crop", "kaleido": "Kaleidoscope", "zoom": "Zoom_Volumetric", "vhs": "VHS_Glitch",
              "trails": "Trails"}

IDS = {"readout": "110", "timecode": "111", "wash": "114", "bars": "120", "winC": "130", "winB": "131", "winA": "132",
       "inside": "140", "title": "145", "data": "150", "waves": "160", "lattice": "170", "sound": "190"}
VISUAL = ["readout", "timecode", "wash", "bars", "winC", "winB", "winA", "inside", "title", "data", "waves", "lattice"]
WINDOWS = ["winA", "winB", "winC"]

WHITE, CYAN, RED = (1, 1, 1), (0.55, 0.86, 1.0), (1.0, 0.12, 0.08)
AMBER, DEEP_BLUE, MAGENTA, ORANGE, ICE = (1.0, 0.62, 0.22), (0.03, 0.07, 0.42), (1.0, 0.18, 0.62), (1.0, 0.58, 0.12), (0.4, 0.62, 1.0)
CORNERS = [f"corner/{c}/{a}" for c in ("top_left", "top_right", "bottom_right", "bottom_left") for a in "xy"]


def shader_path(rel):
    return f"shaders/{Path(rel).name}"


def fx(key, enable=False, **params):
    path = shader_path(EFFECTS[key])
    return {"path": path, "relative_path": path}


def color(prefix, rgb, alpha=1.0):
    return {f"{prefix}/{c}": v for c, v in zip("rgba", (*rgb, alpha))}


def layer(key, name, source, params, effects=(), matte=None):
    out = {"id": IDS[key], "name": name, "enable": True, "locked": False, "viewports": {}, "source": source,
           "fx": [fx(e) for e in effects], "params": params}
    if matte:
        out["matte"] = matte
    return out


def generator(key, extra=None):
    path = shader_path(GENERATORS[key])
    src = {"type": "isf", "path": path, "relative_path": path, "width": W, "height": H}
    if extra:
        src.update(extra)
    return src


def text_params(x, y, w, h, size, align, v_align, content=""):
    return {"opacity": 0, "blend_mode": "normal", "text/content": content, "text/font": "Menlo", "text/size": size,
            "text/align": align, "text/v_align": v_align, "text/width": w, "text/height": h,
            "position/x": x + w / 2, "position/y": y + h / 2, "scale/x": 100 * w / W, "scale/y": 100 * h / H,
            **color("text/color", WHITE)}


def crop(key, top=1.0, bottom=0.0, left=0.0, right=1.0, feather=0.0):
    seg = FX_SEGMENT["crop"]
    return {f"fx/{seg}/enable": True, f"fx/{seg}/param/Top": top, f"fx/{seg}/param/Bottom": bottom,
            f"fx/{seg}/param/Left": left, f"fx/{seg}/param/Right": right,
            **{f"fx/{seg}/param/{side}Feather": feather for side in ("Top", "Bottom", "Left", "Right")}}


# The look every scene starts from: nothing shown, every value at rest. A scene is that, with what it changes.
REST = {
    "readout": text_params(64, 56, 1100, 260, 22, "left", "top"),
    "timecode": {**text_params(W - 64 - 520, H - 56 - 70, 520, 70, 40, "right", "bottom"),
                 "tc/source": "internal", "tc/internal": "free_run", "tc/rate": "25"},
    "bars": {"opacity": 0, "blend_mode": "add", "param/count": 1, "param/width": 0.004, "param/phase": 0,
             "param/speed": 0, "param/angle": 90, "param/spread": 1, "param/jitter": 0, "param/hit": 0,
             "param/strobe": 0, "param/mirror": False, "param/outline": False, "param/grid": False,
             **color("param/tint", WHITE)},
    "data": {"opacity": 0, "blend_mode": "difference", "param/mode": 0, "param/rows": 24, "param/density": 0.5,
             "param/speed": 0.6, "param/flicker": 0, "param/band": 1, "param/center": 0.5, "param/hit": 0,
             "param/glitch": 0, "param/invert": False, "param/progress": 0, **color("param/tint", WHITE),
             f"fx/{FX_SEGMENT['zoom']}/enable": False, f"fx/{FX_SEGMENT['zoom']}/param/DEPTH": 2.2,
             f"fx/{FX_SEGMENT['zoom']}/param/GAIN": 0.28, f"fx/{FX_SEGMENT['zoom']}/param/LAYERS": 12,
             f"fx/{FX_SEGMENT['zoom']}/param/TILE": True,
             f"fx/{FX_SEGMENT['vhs']}/enable": False, f"fx/{FX_SEGMENT['vhs']}/param/analogDistort": 0,
             f"fx/{FX_SEGMENT['vhs']}/param/bleedAmount": 0.6, f"fx/{FX_SEGMENT['vhs']}/param/grainLevel": 0.4},
    "waves": {"opacity": 0, "blend_mode": "add", "param/lines": 40, "param/amplitude": 0.8, "param/depth": 0.7,
              "param/tilt": 0.35, "param/thickness": 1.6, "param/hit": 0, **color("param/tint", WHITE)},
    "lattice": {"opacity": 0, "blend_mode": "normal", "param/speed": 0.5, "param/cell": 2, "param/thickness": 0.012,
                "param/glow": 0.5, "param/fog": 0.45, "param/warp": 0, "param/axes": 0, "param/fov": 70,
                "param/hit": 0, "param/invert": False, **color("param/tint", WHITE),
                "rotation": 0, "scale/x": 100, "scale/y": 100,
                f"fx/{FX_SEGMENT['kaleido']}/enable": False, f"fx/{FX_SEGMENT['kaleido']}/param/segments": 6,
                f"fx/{FX_SEGMENT['kaleido']}/param/spin": 0.05,
                f"fx/{FX_SEGMENT['trails']}/enable": False, f"fx/{FX_SEGMENT['trails']}/param/decay": 0.86,
                f"fx/{FX_SEGMENT['trails']}/param/mode": 0},
    **{w: {"opacity": 0, "blend_mode": "normal", **crop(w, 0.5, 0.5), "rotation": 0, **{c: 0 for c in CORNERS}} for w in WINDOWS},
    # A wash of colour over the picture (multiply: white takes its colours, black stays black)
    "wash": {"opacity": 0, "blend_mode": "multiply", "param/offset": 0.5, "param/frequency": 1, "param/curve": 0,
             "param/vertical": True, **color("param/startColor", WHITE), **color("param/endColor", WHITE)},
    # The title: huge letters, a picture inside them (the layer under the matte), another around them
    "title": {**text_params(0, 0, W, H, 440, "center", "middle", "NyxMap"), "text/font": "Helvetica Neue",
              "text/bold": True, "text/letter_spacing": -12, **color("text/color", DEEP_BLUE), **{c: 0 for c in CORNERS}},
    "inside": {"opacity": 0, "blend_mode": "screen"},
}
# Values owned by timelines or interactions, never set by a scene: the drift of the lattice (Drift), the frames of the
# windows (Reframing, or a scene that sets them itself: see `frames`)
OWNED = {("lattice", "param/yaw"), ("lattice", "param/pitch"), ("lattice", "param/roll")}


def frame(x, y, w, h, roi):
    """A window: its place on the screen (pixels, top left) and the part of its source it shows (0..1, top left)."""
    left, top, right, bottom = roi
    return {"position/x": x + w / 2, "position/y": y + h / 2, "scale/x": 100 * w / W, "scale/y": 100 * h / H,
            "roi/left": left, "roi/top": top, "roi/right": right, "roi/bottom": bottom}


def build_layers(audio_path):
    layers = [
        layer("readout", "Readout", {"type": "text"}, dict(REST["readout"])),
        layer("timecode", "Timecode", {"type": "text", "timecode": {"source": "internal", "internal": "free_run",
                                                                    "rate": "25", "offset": 0}},
              {k: v for k, v in REST["timecode"].items() if not k.startswith("tc/")}),
        layer("wash", "Colour Wash", generator("wash"), dict(REST["wash"])),
        layer("bars", "Light Bars", generator("bars"), dict(REST["bars"])),
        layer("winC", "Window C · data", {"type": "layer", "layer": IDS["data"], "tap": "prefx"},
              {**REST["winC"], **frame(1290, 0, 630, H, (0.67, 0, 1, 1))}, ["crop"]),
        layer("winB", "Window B · lattice", {"type": "layer", "layer": IDS["lattice"], "tap": "postfx"},
              {**REST["winB"], **frame(645, 0, 630, H, (0.335, 0, 0.665, 1))}, ["crop"]),
        layer("winA", "Window A · lattice", {"type": "layer", "layer": IDS["lattice"], "tap": "postfx"},
              {**REST["winA"], **frame(0, 0, 630, H, (0, 0, 0.33, 1))}, ["crop"]),
        layer("inside", "Inside the title · waves", {"type": "layer", "layer": IDS["waves"], "tap": "postfx"},
              dict(REST["inside"]),
              matte={"layer": IDS["title"], "channel": "alpha", "invert": False, "space": "composition"}),
        layer("title", "Title", {"type": "text"}, dict(REST["title"])),
        layer("data", "Data Stream", generator("data", {"audio_sources": {"fft": "program"}}), dict(REST["data"]),
              ["zoom", "vhs"]),
        layer("waves", "Waveform Lines", generator("waves", {"audio_sources": {"wave": "program"}}),
              dict(REST["waves"])),
        layer("lattice", "Beam Lattice", generator("lattice"), dict(REST["lattice"]), ["kaleido", "trails"]),
        {"id": IDS["sound"], "name": "Soundtrack", "enable": True, "locked": False, "viewports": {},
         "source": {"type": "audio", "path": audio_path, "relative_path": audio_path} if audio_path else {"type": "none"},
         "play": False, "fx": [], "params": {"play": False, "play_mode": "oneshot", "volume": 1}},
    ]
    return layers


# ---------------------------------------------------------------------------------------------------------------------
# Interactions: the sound drives values all along; the scenes switch them on and off
# ---------------------------------------------------------------------------------------------------------------------
def remap(a, b, c, d):
    return {"type": "remap", "a": a, "b": b, "c": c, "d": d}


def audio(iid, name, lo, hi, target, param, filters, feature=0):
    return {"id": iid, "name": name, "enabled": False, "source": "audio", "audio_low": lo, "audio_high": hi,
            "audio_feature": feature, "audio_input": False, "layer": IDS[target], "param": param, "master": 1,
            "filters": filters, "takeover": "jump"}


PULSE = {"type": "pulse_detection", "a": 0.1, "b": 0.16}
INTERACTIONS = [
    audio("1", "Kick → bars flash", 35, 110, "bars", "param/hit", [PULSE, remap(0, 0.8, 0, 1)]),
    audio("2", "Kick → lattice flash", 35, 110, "lattice", "param/hit", [dict(PULSE, b=0.28), remap(0, 0.8, 0, 1)]),
    audio("3", "Air → data flicker", 5000, 14000, "data", "param/flicker",
          [{"type": "weighted_average", "a": 0.04}, remap(0.0005, 0.009, 0, 0.55)]),
    audio("4", "Mids → wave height", 300, 2500, "waves", "param/amplitude",
          [{"type": "attack_release", "a": 0.01, "b": 0.18}, remap(0.012, 0.07, 0.35, 2.2)]),
    audio("5", "Level → lattice glow", 20, 20000, "lattice", "param/glow",
          [{"type": "attack_release", "a": 0.02, "b": 0.3}, remap(0.05, 0.6, 0.25, 1.6)], feature=1),
    audio("6", "Kick → data inverts", 35, 110, "data", "param/invert",
          [PULSE, {"type": "toggle", "a": 0.5}]),
    audio("7", "Kick → waves jump", 35, 110, "waves", "param/hit", [dict(PULSE, b=0.2), remap(0, 0.8, 0, 0.8)]),
    # The soundtrack's own time, read: its progress along the data's bottom line, and a slow roll of the lattice
    {"id": "8", "name": "Soundtrack progress → data line", "enabled": True, "source": "internal",
     "source_layer": IDS["sound"], "source_param": "progress", "layer": IDS["data"], "param": "param/progress",
     "master": 1, "filters": [], "takeover": "jump"},
    {"id": "9", "name": "Soundtrack progress → lattice roll", "enabled": True, "source": "internal",
     "source_layer": IDS["sound"], "source_param": "progress", "layer": IDS["lattice"], "param": "param/roll",
     "master": 1, "filters": [remap(0, 1, 0, -38)], "takeover": "jump"},
    # Action: a burst in the highs tears the data (at once), and lets it heal (over 0.35 s: the action's time)
    {"id": "10", "name": "Highs burst → glitch", "enabled": False, "source": "audio", "audio_low": 6000,
     "audio_high": 16000, "audio_feature": 0, "audio_input": False, "layer": "0", "param": "", "master": 1,
     "filters": [], "takeover": "jump", "mode": "action",
     "conditions": [{"op": "ge", "a": 0.006, "b": 1}], "conditions_all": True,
     "if_true": [{"layer": IDS["data"], "param": f"fx/{FX_SEGMENT['vhs']}/param/analogDistort", "value": 7, "time": 0},
                 {"layer": IDS["data"], "param": "param/glitch", "value": 0.9, "time": 0}],
     "if_false": [{"layer": IDS["data"], "param": f"fx/{FX_SEGMENT['vhs']}/param/analogDistort", "value": 0,
                   "time": 0.35, "curve": 2},
                  {"layer": IDS["data"], "param": "param/glitch", "value": 0, "time": 0.5, "curve": 2}]},
    # Action on the kick, several consequences: at once on the hit, each back at its own pace (its action's time)
    {"id": "12", "name": "Kick → punch", "enabled": False, "source": "audio", "audio_low": 35, "audio_high": 110,
     "audio_feature": 0, "audio_input": False, "layer": "0", "param": "", "master": 1, "filters": [],
     "takeover": "jump", "mode": "action", "conditions": [{"op": "ge", "a": 0.36, "b": 1}], "conditions_all": True,
     "if_true": [{"layer": IDS["lattice"], "param": "param/fov", "value": 108, "time": 0},
                 {"layer": IDS["lattice"], "param": "param/cell", "value": 1.5, "time": 0},
                 {"layer": IDS["bars"], "param": "param/jitter", "value": 0.5, "time": 0},
                 {"layer": IDS["waves"], "param": "param/thickness", "value": 5, "time": 0}],
     "if_false": [{"layer": IDS["lattice"], "param": "param/fov", "value": 70, "time": 0.5, "curve": 2},
                  {"layer": IDS["lattice"], "param": "param/cell", "value": 2, "time": 0.8, "curve": 3},
                  {"layer": IDS["bars"], "param": "param/jitter", "value": 0, "time": 0.25, "curve": 0},
                  {"layer": IDS["waves"], "param": "param/thickness", "value": 1.6, "time": 0.4, "curve": 2}]},
    # Action on the soundtrack's playhead: past its last note, the show goes to black by itself; rewound, it comes back
    {"id": "11", "name": "Tape end → blackout", "enabled": True, "source": "internal", "source_layer": IDS["sound"],
     "source_param": "playhead", "layer": "0", "param": "", "master": 1, "filters": [], "takeover": "jump",
     "mode": "action", "conditions": [{"op": "ge", "a": 29.4, "b": 1}], "conditions_all": True,
     "if_true": [{"layer": "0", "param": "show/blackout", "value": 1}],
     "if_false": [{"layer": "0", "param": "show/blackout", "value": 0}]},
]
KICKS = {"1", "2", "6", "7"}


# ---------------------------------------------------------------------------------------------------------------------
# Timelines
# ---------------------------------------------------------------------------------------------------------------------
def track(key, param, keys=None, oscillator=None):
    t = {"layer": IDS[key], "param": param}
    if keys is not None:
        t["keys"] = keys
    if oscillator:
        t["oscillator"] = oscillator
    return t


def held(values, step=BEAT):
    """A value per beat, held until the next (a cut on each beat)."""
    return [[round(i * step, 4), v, "hold"] for i, v in enumerate(values)]


# The windows' frames on each beat of a bar: a triptych, a zoom with insets, strips, a full crop
REFRAMES = {
    "winA": [frame(0, 0, 630, H, (0.0, 0, 0.33, 1)), frame(0, 0, 1280, H, (0.3, 0.15, 0.6, 0.85)),
             frame(0, 0, W, 360, (0.0, 0.3, 1, 0.63)), frame(0, 0, W, H, (0.36, 0.36, 0.64, 0.64))],
    "winB": [frame(645, 0, 630, H, (0.335, 0, 0.665, 1)), frame(1300, 60, 560, 460, (0.7, 0.1, 0.99, 0.55)),
             frame(0, 360, W, 360, (0.0, 0.0, 1, 0.33)), frame(1420, 760, 440, 260, (0.0, 0.0, 0.25, 0.25))],
    "winC": [frame(1290, 0, 630, H, (0.67, 0, 1, 1)), frame(1300, 560, 560, 460, (0.0, 0.5, 0.3, 0.95)),
             frame(0, 720, W, 360, (0.0, 0.67, 1, 1)), frame(60, 760, 440, 260, (0.4, 0.4, 0.62, 0.62))],
}


# ...each tilted a little differently on each beat
for w, angles in {"winA": [0, -6, 0, 4], "winB": [0, 8, 0, -12], "winC": [0, -4, 0, 10]}.items():
    for f, a in zip(REFRAMES[w], angles):
        f["rotation"] = a


def reframing_tracks():
    tracks = []
    for w, frames in REFRAMES.items():
        for param in frames[0]:
            tracks.append(track(w, param, held([f[param] for f in frames])))
    # Each window opens from its middle line on the bar's first beat, and stays open
    seg = FX_SEGMENT["crop"]
    for i, w in enumerate(WINDOWS):
        tracks.append(track(w, f"fx/{seg}/param/Top", [[0, 0.5], [0.12 + 0.05 * i, 1.0, 2]]))
        tracks.append(track(w, f"fx/{seg}/param/Bottom", [[0, 0.5], [0.12 + 0.05 * i, 0.0, 2]]))
    return tracks


TIMELINES = [
    {"id": "1", "name": "Rise", "duration": 1.5, "loop": "once",
     "tracks": [track("data", "param/band", [[0, 0.06], [1.5, 0.55, 1]]),
                track("data", "param/rows", [[0, 6], [1.5, 40, 1]]),
                track("bars", "param/spread", [[0, 0.065], [1.5, 1.0, 4]]),
                track("bars", "param/count", [[0, 2], [1.5, 24, 4]])]},
    {"id": "2", "name": "Drift", "duration": 24.6, "loop": "loop",
     "tracks": [track("lattice", "param/yaw", oscillator={"wave": "sine", "period": 24 * BEAT, "center": 0,
                                                          "amplitude": 34, "phase": 0, "seed": 1}),
                track("lattice", "param/pitch", oscillator={"wave": "sine", "period": 16 * BEAT, "center": 0,
                                                            "amplitude": 14, "phase": 0.25, "seed": 2})]},
    {"id": "3", "name": "Reframing", "duration": round(4 * BEAT, 4), "loop": "loop", "tracks": reframing_tracks()},
    {"id": "4", "name": "Sweep", "duration": round(4 * BEAT, 4), "loop": "loop",
     "tracks": [track("bars", "param/angle", held([0, 45, 90, -45])),
                track("bars", "param/phase", [[0, 0], [round(4 * BEAT, 4), 4, 0]])]},
    {"id": "5", "name": "Fade out", "duration": 2.3, "loop": "once",
     "tracks": [track("bars", "opacity", [[0, 1], [0.05, 0, "hold"]]),
                *[track(w, "opacity", [[0, 1], [0.77 + 0.12 * i, 0, "hold"]]) for i, w in enumerate(WINDOWS)],
                track("data", "opacity", [[0, 0.8], [1.54, 0, "hold"]]),
                track("waves", "opacity", [[0, 0.6], [1.92, 0, "hold"]]),
                track("lattice", "opacity", [[0, 1], [2.3, 0.25, 2]])]},
]
# The title leans back into the depth, as a floor seen from above, then turns
TIMELINES.append({"id": "6", "name": "Title tilt", "duration": 1.5, "loop": "once",
                  "tracks": [track("title", "corner/top_left/x", [[0, 0], [1.5, 16, 3]]),
                             track("title", "corner/top_right/x", [[0, 0], [1.5, -16, 3]]),
                             track("title", "corner/top_left/y", [[0, 0], [1.5, 10, 3]]),
                             track("title", "corner/top_right/y", [[0, 0], [1.5, 2, 3]]),
                             track("title", "corner/bottom_right/x", [[0, 0], [1.5, 6, 3]])]})
# The corridor: two walls and a floor mapped in perspective, the vanishing point swinging on the beat
SWING = round(4 * BEAT, 4)
TIMELINES.append({"id": "7", "name": "Corridor", "duration": SWING, "loop": "loop",
                  "tracks": [track("winA", "corner/top_right/y", [[0, 30], [SWING / 2, 40, 3], [SWING, 30, 3]]),
                             track("winA", "corner/bottom_right/y", [[0, -30], [SWING / 2, -20, 3], [SWING, -30, 3]]),
                             track("winA", "corner/top_right/x", [[0, 0], [SWING / 2, -30, 3], [SWING, 0, 3]]),
                             track("winA", "corner/bottom_right/x", [[0, 0], [SWING / 2, -30, 3], [SWING, 0, 3]]),
                             track("winB", "corner/top_left/y", [[0, 30], [SWING / 2, 20, 3], [SWING, 30, 3]]),
                             track("winB", "corner/bottom_left/y", [[0, -30], [SWING / 2, -40, 3], [SWING, -30, 3]]),
                             track("winB", "corner/top_left/x", [[0, 0], [SWING / 2, -30, 3], [SWING, 0, 3]]),
                             track("winB", "corner/bottom_left/x", [[0, 0], [SWING / 2, -30, 3], [SWING, 0, 3]]),
                             track("winC", "corner/top_left/x", [[0, 34], [SWING / 2, 22, 3], [SWING, 34, 3]]),
                             track("winC", "corner/top_right/x", [[0, -34], [SWING / 2, -46, 3], [SWING, -34, 3]])]})
# The tunnel turns: the lattice layer itself rotates, a full turn every two bars
TIMELINES.append({"id": "8", "name": "Spin", "duration": round(8 * BEAT, 4), "loop": "loop",
                  "tracks": [track("lattice", "rotation", [[0, -180], [round(8 * BEAT, 4), 180, 0]])]})
TL = {t["name"]: t["id"] for t in TIMELINES}

# The corridor's planes: where each window is before its corners are pulled
CORRIDOR = {
    "winA": {**frame(0, 0, 960, 760, (0.0, 0.0, 0.5, 1.0)), "corner/top_right/y": 30, "corner/bottom_right/y": -30},
    "winB": {**frame(960, 0, 960, 760, (0.5, 0.0, 1.0, 1.0)), "corner/top_left/y": 30, "corner/bottom_left/y": -30},
    "winC": {**frame(0, 760, W, 320, (0.0, 0.4, 1.0, 1.0)), "corner/top_left/x": 34, "corner/top_right/x": -34},
}


# ---------------------------------------------------------------------------------------------------------------------
# Presets: a few values at once, recalled by a step for the fastest moves
# ---------------------------------------------------------------------------------------------------------------------
def preset(pid, name, values, time=0.0):
    return {"id": pid, "name": name, "fade": time,
            "values": [{"layer": IDS[k], "path": p, "value": v, "time": time} for k, p, v in values],
            "animations": [], "interactions": [], "geometry": {}, "categories": {}}


PRESETS = [
    preset("1", "Flash", [("bars", "opacity", 1), ("bars", "param/count", 1), ("bars", "param/width", 1),
                          ("bars", "param/angle", 0), ("bars", "param/spread", 1), ("bars", "param/strobe", 0), ("bars", "param/jitter", 0),
                          ("bars", "param/grid", False), ("bars", "param/tint/r", 1), ("bars", "param/tint/g", 1),
                          ("bars", "param/tint/b", 1)]),
    preset("2", "Cut to black", [(k, "opacity", 0) for k in VISUAL if k not in ("readout", "timecode")]),
]
PR = {p["name"]: p["id"] for p in PRESETS}


# ---------------------------------------------------------------------------------------------------------------------
# The scenes (snapshots) and when they play
# ---------------------------------------------------------------------------------------------------------------------
class Scene:
    def __init__(self, name, fade, values, on=(), timing=None, easing=None, frames=False, audio_start=False):
        self.name, self.fade, self.values, self.on = name, fade, values, set(on)
        self.timing, self.easing = timing or {}, easing or {}
        self.frames, self.audio_start = frames, audio_start


def v(key, **params):
    """Values of a layer: v("bars", opacity=1, count=3) — a name without a slash is its shader's parameter."""
    out = {}
    for name, value in params.items():
        path = name.replace("__", "/")
        if path not in ("opacity", "blend_mode") and "/" not in path:
            path = f"param/{path}"
        if isinstance(value, tuple):  # a colour
            out.update({(key, f"{path}/{c}"): x for c, x in zip("rgb", value)})
        else:
            out[(key, path)] = value
    return out


def text(content, opacity=1.0):
    return {("readout", "text/content"): content, ("readout", "opacity"): opacity}


def fxv(key, effect, **params):
    seg = FX_SEGMENT[effect]
    return {(key, f"fx/{seg}/{'enable' if n == 'enable' else 'param/' + n}"): x for n, x in params.items()}


def merge(*parts):
    out = {}
    for p in parts:
        out.update(p)
    return out


TC = {("timecode", "opacity"): 0.75}
SCENES = {
    "ready": Scene("00 · Ready", 0, merge(text("NyxMap\n— stand by"), {("timecode", "opacity"): 0.4}),
                   audio_start=True),
    "signal": Scene("01 · Signal", 0, merge(text("NyxMap\nSIGNAL 01 · 78 BPM"), TC, v("bars", opacity=1)), on={"1"}),
    "split": Scene("02 · Split", 0.9, merge(text("SIGNAL 02 · SPLIT"), TC,
                                            v("bars", opacity=1, count=3, width=0.006, spread=0.22)),
                   on={"1"}, easing={("bars", "param/spread"): "ease_out"}),
    "data": Scene("03 · Data", 0, merge(text("DATA 0x3F2A\nSTREAM · BARCODE"), TC,
                                        v("bars", opacity=1, count=2, width=0.003, spread=0.065),
                                        v("data", opacity=1, band=0.06, rows=6, density=0.5, speed=0.9,
                                          blend_mode="normal")), on={"1", "3"}),
    "structure": Scene("04 · Structure", 0.6, merge(text("STRUCTURE\n6.20 · BASS"), TC,
                                                    v("lattice", opacity=1, speed=0.5),
                                                    v("wash", opacity=1, startColor=ICE, endColor=WHITE),
                                                    v("data", opacity=1, band=1, rows=48, density=0.12, speed=0.3)),
                       on={"12", "2", "3", "5"}, timing={("data", "param/band"): 0.0}),
    "windows": Scene("05 · Windows", 0, merge(text("REFRAME\nA · B · C"), TC,
                                              v("winA", opacity=1), v("winB", opacity=1), v("winC", opacity=1),
                                              v("lattice", opacity=0, speed=0.9),
                                              v("data", opacity=0, band=1, rows=36, density=0.45, speed=1.0)),
                     on={"2", "3", "5"}),
    "density": Scene("06 · Density", 0.4, merge(text("DENSITY\nMATRIX 36 × 64"), TC,
                                                v("winA", opacity=1), v("winB", opacity=1), v("winC", opacity=1),
                                                v("lattice", opacity=0, speed=1.4, warp=0.5),
                                                v("data", opacity=0, mode=1, rows=36, density=0.4, speed=1.0)),
                     on={"2", "3", "5"}),
    "impact": Scene("07 · Impact", 0.5, merge(text("IMPACT\n11.59"), TC,
                                              v("lattice", opacity=1, speed=2.2, invert=True, warp=0.8),
                                              v("bars", opacity=0)),
                    on={"5"}, timing={("bars", "opacity"): 0.45, ("lattice", "param/invert"): 0.0},
                    easing={("bars", "opacity"): "ease_out"}),
    "wave": Scene("08 · Wave", 0.8, merge(text("WAVE\nPROGRAM · 2 CH"), TC,
                                          v("lattice", opacity=0.35, speed=0.4),
                                          v("waves", opacity=1, tint=CYAN),
                                          v("data", opacity=1, mode=2, band=0.06, center=0.06, rows=5,
                                            blend_mode="normal")),
                  on={"12", "4", "7", "3", "5"}, timing={("lattice", "param/invert"): 0.0}),
    "title": Scene("09 · Title", 0.6, merge(text("NyxMap\nINSIDE · AROUND"), TC,
                                            v("lattice", opacity=1, speed=0.3, glow=0.9, tint=AMBER),
                                            v("title", opacity=1), v("inside", opacity=1),
                                            v("waves", opacity=0, tint=(0.75, 0.95, 1.0), lines=64, depth=0.85, amplitude=1.4,
                                              thickness=2.6)),
                   on={"12", "4", "7", "5"}, timing={("title", "opacity"): 0.0, ("inside", "opacity"): 0.0}),
    "tunnel": Scene("10 · Tunnel", 0, merge(text("TUNNEL\nAXIS Z · 8 SEGMENTS"), TC,
                                            v("bars", opacity=0.8, count=14, width=0.05, mirror=True, spread=1),
                                            v("lattice", opacity=1, axes=2, speed=3.5, fov=115, glow=1.1,
                                              thickness=0.02, tint=ICE, scale__x=170, scale__y=170),
                                            fxv("lattice", "kaleido", enable=True, segments=8),
                                            fxv("lattice", "trails", enable=True)),
                    on={"12", "1", "2"}),
    "saturation": Scene("11 · Saturation", 0, merge(text("SATURATION\n///////////////"), TC,
                                                    v("bars", opacity=1, count=6, width=0.12, strobe=round(4 / BEAT, 3),
                                                      tint=RED, angle=0),
                                                    v("lattice", opacity=1, speed=3.2, warp=1.1),
                                                    v("data", opacity=1, band=1, rows=64, density=0.55, speed=1.8),
                                                    fxv("data", "zoom", enable=True),
                                                    fxv("data", "vhs", enable=True)),
                        on={"12", "2", "6", "10", "5"}),
    "fall": Scene("12 · Fall", 0.8, merge(text("FALL"), TC,
                                          v("bars", opacity=0.6, count=3, width=0.08, tint=RED, angle=0),
                                          v("lattice", opacity=1, speed=0.6, warp=0.4),
                                          v("data", opacity=1, band=1, rows=64, density=0.4, speed=0.8),
                                          fxv("data", "zoom", enable=True, GAIN=0.18), fxv("data", "vhs", enable=True)),
                  on={"2", "6", "10"}, easing={("lattice", "param/speed"): "ease_out"}),
    "void": Scene("13 · Void", 0, merge(text("—"), TC, v("bars", opacity=1, width=0.002))),
    "corridor": Scene("14 · Corridor", 0.4, merge(text("CORRIDOR\nTHREE PLANES · MAPPED"), TC,
                                                  *[v(w, opacity=1, fx__Soft_Crop__param__Top=1, fx__Soft_Crop__param__Bottom=0)
                                                    for w in WINDOWS],
                                                  v("lattice", opacity=0, speed=1.0),
                                                  v("data", opacity=0, mode=1, rows=20, density=0.45, speed=1.2),
                                                  v("waves", opacity=1, amplitude=1.1, depth=0.9),
                                                  v("wash", opacity=1, startColor=MAGENTA, endColor=ORANGE)),
                      on={"12", "4", "2", "3", "7"}, frames=CORRIDOR),
    "grid": Scene("15 · Grid", 0, merge(text("GRID\n6 × 6 · OUTLINE"), TC,
                                        v("bars", opacity=1, grid=True, outline=True, count=6, width=0.98, angle=0),
                                        v("lattice", opacity=0, axes=1, speed=1.2),
                                        v("winA", opacity=1, **{"fx__Soft_Crop__param__Top": 1, "fx__Soft_Crop__param__Bottom": 0}),
                                        v("winB", opacity=1, **{"fx__Soft_Crop__param__Top": 1, "fx__Soft_Crop__param__Bottom": 0}),
                                        v("winC", opacity=1, **{"fx__Soft_Crop__param__Top": 1, "fx__Soft_Crop__param__Bottom": 0}),
                                        v("data", opacity=0, mode=1, rows=24, density=0.5)),
                  on={"1", "2"}, frames=True),
    "rupture": Scene("16 · Rupture", 0, merge(text("RUPTURE\nERR 0x00FF"), TC,
                                              v("data", opacity=1, band=1, rows=40, density=0.4, speed=1.4,
                                                glitch=0.6),
                                              fxv("data", "vhs", enable=True),
                                              v("wash", opacity=1, startColor=CYAN, endColor=MAGENTA),
                                              v("lattice", opacity=1, speed=1.6, warp=1.6)),
                     on={"12", "2", "3", "10", "5"}),
    "finale": Scene("17 · Finale", 0, merge(text("FINALE\nALL CHANNELS"), TC,
                                            v("winA", opacity=1), v("winB", opacity=1), v("winC", opacity=1),
                                            v("bars", opacity=1, count=4, width=0.1, strobe=round(8 / BEAT, 3), angle=0),
                                            v("lattice", opacity=1, speed=2.0),
                                            v("waves", opacity=0.6),
                                            v("data", opacity=0.8, band=1, rows=48, density=0.5, speed=1.6),
                                            v("wash", opacity=1, startColor=ORANGE, endColor=WHITE),
                                            fxv("lattice", "trails", enable=True),
                                            fxv("data", "zoom", enable=True)),
                     on={"12", "1", "2", "4", "5"}),
    "last": Scene("18 · Last signal", 0.9, merge(text("NyxMap\nEND OF SIGNAL"), TC, v("bars", opacity=1, width=0.003)),
                  timing={("bars", "opacity"): 0.0}),
    "black": Scene("19 · Black", 1.4, merge(text(""), {("timecode", "opacity"): 0})),
}

# The sequence: GO on the first step starts the soundtrack; its playhead plays every step that has a timecode. A step
# after one that follows (no timecode) comes at once: a timeline started or stopped with its scene.
STEPS = [
    ("scene", "ready", None, "GO: the soundtrack starts"),
    ("scene", "signal", beat(0), "one line, the kick lights it"),
    ("scene", "split", beat(2), "three lines open"),
    ("scene", "data", beat(4), "a band of data"),
    ("play", "Rise", beat(6), "the band rises to the bass"),
    ("scene", "structure", beat(8), "the bass: the lattice"),
    ("play", "Drift", None, ""),
    ("scene", "windows", beat(10), "three windows, reframed each beat"),
    ("play", "Reframing", None, ""),
    ("scene", "density", beat(12), "the data turns to a matrix"),
    ("preset", "Flash", beat(15) - 0.03, "the hit: white"),
    ("scene", "impact", None, "inverted lattice"),
    ("stop", "Reframing", None, ""),
    ("scene", "wave", beat(16), "the waveform, cyan"),
    ("scene", "title", beat(18), "the title: waves inside, amber lattice around"),
    ("play", "Title tilt", None, ""),
    ("scene", "tunnel", beat(20), "the tunnel turns, blades on each beat"),
    ("play", "Sweep", None, ""),
    ("play", "Spin", None, ""),
    ("scene", "saturation", beat(22), "red strobe, zoom, glitch bursts"),
    ("stop", "Sweep", None, ""),
    ("stop", "Spin", None, ""),
    ("scene", "fall", beat(24), "slowing down"),
    ("scene", "void", beat(25), "silence: one line"),
    ("scene", "corridor", beat(26), "a corridor of mapped planes, its vanishing point swinging"),
    ("play", "Corridor", None, ""),
    ("scene", "grid", beat(28), "the grid, three fixed windows"),
    ("stop", "Corridor", None, ""),
    ("preset", "Cut to black", 22.43, "a hole in the music"),
    ("scene", "rupture", beat(30), "tearing"),
    ("scene", "finale", beat(32), "everything, the windows twice as fast"),
    ("play", "Reframing", None, ""),
    ("speed", "Reframing", None, ""),
    ("play", "Fade out", beat(33) + 0.1, "the layers leave one by one"),
    ("stop", "Reframing", None, ""),
    ("stop", "Fade out", beat(36) - 0.03, ""),
    ("preset", "Flash", None, "the last hit: white"),
    ("scene", "last", None, "end of signal"),
    ("scene", "black", 28.45, "black"),
    ("stop", "Drift", None, ""),
]
FOLLOWED_BY_PRESET_SCENE = 0.1  # a flash lasts this long before the scene after it


def scene_state(base_layers, scene, interaction_ids):
    """A snapshot: every layer at rest, then what the scene changes; the interactions it switches on."""
    states = []
    for lay in base_layers:
        key = next(k for k, i in IDS.items() if i == lay["id"])
        state = copy.deepcopy(lay)
        if key == "sound":
            state["included"] = scene.audio_start
            if scene.audio_start:
                state["params"] = {"restart": True, "play": True, "play_mode": "oneshot", "volume": 1}
            states.append(state)
            continue
        params = {p: x for p, x in REST.get(key, {}).items() if (key, p) not in OWNED}
        if key in WINDOWS and not scene.frames:  # the Reframing timeline frames them, or the frames they have
            params = {p: x for p, x in params.items() if not p.startswith(("position/", "roi/", "scale/", "fx/"))}
            params["opacity"] = 0
        if key in WINDOWS and scene.frames:
            params.update(REFRAMES[key][0] if scene.frames is True else scene.frames[key])
        if key == "timecode":
            params = {p: x for p, x in params.items() if not p.startswith("tc/")}
            if scene.audio_start:
                params.update({"tc/position": 0, "tc/play": True})
        for (k, p), x in scene.values.items():
            if k == key:
                params[p] = x
        state["params"] = params
        state["included"] = True
        timing = {p: t for (k, p), t in scene.timing.items() if k == key}
        if key == "readout":  # its text typed in, whatever the scene's fade
            timing.setdefault("text/content", 0.35)
        easing = {p: e for (k, p), e in scene.easing.items() if k == key}
        if timing:
            state["timing"] = timing
        if easing:
            state["easing"] = easing
        states.append(state)
    interactions = []
    for it in INTERACTIONS:
        if it["id"] in ("8", "9", "11"):
            interactions.append({"id": it["id"], "included": False})
            continue
        interactions.append({"id": it["id"], "included": True, "enabled": it["id"] in scene.on})
    return states, interactions


def volatile_values():
    """The values something other than the scenes moves between two steps: the interactions (their targets, their
    actions' consequences), the timelines' tracks, the Flash and Cut to black presets. A scene's preset always holds
    those it sets, changed or not, to put them back as its snapshot would."""
    out = set()
    for it in INTERACTIONS:
        if it.get("param"):
            out.add((it["layer"], it["param"]))
        for c in it.get("if_true", []) + it.get("if_false", []):
            out.add((c["layer"], c["param"]))
    for tl in TIMELINES:
        out.update((t["layer"], t["param"]) for t in tl["tracks"])
    for p in PRESETS:
        out.update((v["layer"], v["path"]) for v in p["values"])
    return out


def scene_values(base_layers, scene):
    """Every value a scene's snapshot sets: (layer, path) → (value, its own time or None, its own easing or None),
    with the interactions it switches on or off as the composition's interaction/<id>/enable."""
    states, interactions = scene_state(base_layers, scene, INTERACTIONS)
    values = {}
    for state in states:
        if state["id"] == IDS["sound"] or not state.get("included", True):
            continue
        timing, easing = state.get("timing", {}), state.get("easing", {})
        for path, value in state["params"].items():
            values[(state["id"], path)] = (value, timing.get(path), easing.get(path))
    for i in interactions:
        if i["included"]:
            values[("0", f"interaction/{i['id']}/enable")] = (i["enabled"], None, None)
    return values


def scene_preset(pid, scene, values, before, volatile):
    """The scene as a preset: what differs from the scene before it, and the values something else may have moved
    since (`volatile`), each with its snapshot's time and easing (the scene's fade unless it has its own)."""
    items = []
    for (layer_id, path), (value, time, easing) in values.items():
        if (layer_id, path) in before and before[(layer_id, path)][0] == value and (layer_id, path) not in volatile:
            continue
        item = {"layer": layer_id, "path": path, "value": value}
        if time is not None:
            item["time"] = time
        if easing is not None:
            item["easing"] = easing
        items.append(item)
    return {"id": pid, "name": scene.name, "fade": scene.fade, "values": items, "animations": [], "interactions": [],
            "geometry": {}, "categories": {}}


def build(out, audio=None, autoplay=False):
    out = Path(out).resolve()
    (out / "shaders").mkdir(parents=True, exist_ok=True)
    for rel in [*GENERATORS.values(), *EFFECTS.values()]:
        shutil.copy2(ISF / rel, out / "shaders" / Path(rel).name)
    audio_path = None
    if audio:
        audio = Path(audio).resolve()
        if not audio.is_file():
            raise SystemExit(f"Soundtrack not found: {audio}")
        (out / "audio").mkdir(exist_ok=True)
        target = out / "audio" / audio.name
        if audio != target.resolve():
            shutil.copy2(audio, target)
        audio_path = f"audio/{audio.name}"

    layers = build_layers(audio_path)
    # The first scene is the one snapshot: the whole show set, the soundtrack started. Every other scene, in the order
    # the sequence plays them, is a preset of what changes from the scene before it, plus the values the interactions,
    # the timelines or the other presets may have moved meanwhile: the same values its snapshot would have left.
    snapshots, presets, scenes = [], list(PRESETS), {}
    volatile, before = volatile_values(), {}
    for key in [what for kind, what, _, _ in STEPS if kind == "scene"]:
        scene = SCENES[key]
        values = scene_values(layers, scene)
        if not snapshots:
            states, interactions = scene_state(layers, scene, INTERACTIONS)
            snapshots.append({"id": "1", "name": scene.name, "fade": scene.fade, "layers": states,
                              "composition": {"included": True, "opacity": 1, "volume": 1},
                              "interactions": interactions})
            scenes[key] = ("snapshot", "1")
        else:
            pid = str(len(presets) + 1)
            presets.append(scene_preset(pid, scene, values, before, volatile))
            scenes[key] = ("preset", pid)
        before = values

    steps = []
    for i, (kind, what, at, comment) in enumerate(STEPS):
        nxt = STEPS[i + 1] if i + 1 < len(STEPS) else None
        follows = nxt is not None and nxt[2] is None
        step = {"text": comment} if comment else {}
        if kind == "scene":
            kind_of, sid = scenes[what]
            step.update({kind_of: sid, "text": SCENES[what].name,
                         **({"description": comment} if comment else {})})
        elif kind == "preset":
            step["preset"] = PR[what]
        else:
            step.update({"timeline": TL[what], "action": {"play": "play", "stop": "stop", "speed": "speed"}[kind]})
            if kind == "speed":
                step["speed"] = 2
        if at is not None:
            step["at"] = round(at, 3)
        if follows:
            step["continue"] = "follow"
            if kind == "preset" and nxt[0] == "scene":
                step["post_wait"] = FOLLOWED_BY_PRESET_SCENE
        steps.append(step)

    project = {
        "app": "NyxMap", "format_version": 1,
        "composition": {"width": W, "height": H, "fps": 60, "speed": 1}, "audio": {"volume": 1},
        "layers": layers, "interactions": INTERACTIONS, "timelines": TIMELINES, "presets": presets,
        "snapshots": snapshots, "snapshot_tree": [s["id"] for s in snapshots],
        "sequences": [{"id": "1", "name": "Demo · on the soundtrack's timecode", "loop": False, "steps": steps,
                       "timecode": {"layer": IDS["sound"], "param": "playhead"}}],
        "current_sequence": 0, "startup_sequence": 0, "startup_step": 0, "startup_go": autoplay,
        "ui": {"selected_layer": 0},
    }
    (out / "Demo.nyxmap").write_text(json.dumps(project, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return project


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--audio", type=Path, help="the soundtrack (30 s, 78 BPM)")
    parser.add_argument("--out", type=Path, required=True, help="the show folder to write")
    parser.add_argument("--autoplay", action="store_true", help="GO on the first step as soon as the project opens")
    args = parser.parse_args()
    p = build(args.out, args.audio, args.autoplay)
    timed = sum(1 for s in p["sequences"][0]["steps"] if "at" in s)
    print(f"Show ready: {args.out.resolve() / 'Demo.nyxmap'} — {len(p['snapshots'])} snapshot, {len(p['presets'])} presets, "
          f"{len(p['timelines'])} timelines, {len(p['interactions'])} interactions, {timed} steps on the timecode")
