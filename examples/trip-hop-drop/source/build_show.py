#!/usr/bin/env python3
"""Trip Hop Drop: the project, from cues.json (when every hit of the soundtrack falls, written by score.cpp).

    python3 build_show.py [--autoplay]

The sequence stands by on its first step; GO starts the soundtrack, and its playhead plays the other steps at their timecode.
The hits are on five empty layers ("Hit · Kick"...), written by timelines. Interactions turn each hit into envelopes on
everything: the deformation, the sizes, the lights, the glow, the white lines and points.
"""
import copy
import json
import math
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SHOW = HERE.parent
REPO = HERE.parents[2]
W, H = 1920, 1080
CUES = json.loads((HERE / "cues.json").read_text())
BAR = CUES["bar"]
AUDIO = "audio/Trip Hop Drop.mp3"

_next = [100]


def new_id():
    _next[0] += 1
    return str(_next[0])


layers = []


def add(layer):
    layers.append(layer)
    return layer["id"]


def empty(name, params=None):
    return add({"id": new_id(), "name": name, "enable": True, "locked": False, "viewports": {}, "source": {"type": "none"}, "fx": [],
                "params": {"opacity": 0, "blend_mode": "add", **(params or {})}})


def generator(name, shader, params, blend="add", opacity=1.0):
    path = f"shaders/{Path(shader).name}"
    shutil.copy2(REPO / "isf" / shader, SHOW / path)
    return add({"id": new_id(), "name": name, "enable": True, "locked": False, "viewports": {},
                "source": {"type": "isf", "path": path, "relative_path": path, "width": W, "height": H}, "fx": [],
                "params": {"opacity": opacity, "blend_mode": blend, **{f"param/{k}" if "/" not in k else k: v for k, v in params.items()}}})


def group3d(name, parent, position=(0, 0, 0), scale=1.0):
    p = {"merge/position/x": position[0], "merge/position/y": position[1], "merge/position/z": position[2], "merge/scale/all": scale}
    return add({"id": new_id(), "name": name, "enable": True, "locked": False, "viewports": {}, "group": True, "merge3d": True, "collapsed": True,
                "parent": parent, "source": {"type": "none"}, "fx": [], "params": p})


def model(name, shape, parent, position=(0, 0, 0), rotation=(0, 0, 0), scale=1.0, axes=(1, 1, 1), color=(0.8, 0.8, 0.8), metal=0.0, rough=0.5,
          detail=1, displace=None, amount=0.2, mid=0.5, unlit=False, smooth=False):
    src = {"type": "model", "path": f"/NyxMap/3D/{shape}"}
    if displace:
        src["displace"] = displace
        src["displace_stage"] = -1
    p = {"model/fit": True, "model/detail": detail, "model/unlit": unlit, "model/scale/all": scale,
         "model/material/metal": metal, "model/material/rough": rough, "model/displace/amount": amount, "model/displace/mid": mid}
    if smooth:
        p["model/shading"] = "smooth"
    for i, a in enumerate("xyz"):
        p[f"model/position/{a}"] = position[i]
        p[f"model/rotation/{a}"] = rotation[i]
        p[f"model/scale/{a}"] = axes[i]
    for i, a in enumerate("rgb"):
        p[f"model/material/color/{a}"] = color[i]
    return add({"id": new_id(), "name": name, "enable": True, "locked": False, "viewports": {}, "parent": parent, "source": src, "fx": [], "params": p})


def light(name, parent, kind, position, color, intensity, level=1.0, look=None, path=None, angle=30.0, falloff=0.4, soft=4.0, rays=1.0, size=(4, 2), yaw=0.0, pitch=-90.0,
          shadow=True):
    p = {"light/type": kind, "light/intensity": intensity, "light/level": level, "light/angle": angle, "light/falloff": falloff, "light/softness": soft, "light/rays": rays,
         "light/shadow": shadow, "light/width": size[0], "light/height": size[1], "light/yaw": yaw, "light/pitch": pitch}
    for i, a in enumerate("xyz"):
        p[f"light/position/{a}"] = position[i]
    for i, a in enumerate("rgb"):
        p[f"light/color/{a}"] = color[i]
    if look is not None:
        p["light/lookat"] = True
        for i, a in enumerate("xyz"):
            p[f"light/lookpoint/{a}"] = look[i]
    if path:
        p["light/path/shape"], p["light/path/width"], p["light/path/depth"], p["light/path/speed"] = path
    return add({"id": new_id(), "name": name, "enable": True, "locked": False, "viewports": {}, "parent": parent, "source": {"type": "light"}, "fx": [], "params": p})


(SHOW / "shaders").mkdir(exist_ok=True)

# ---- The white over the scene, top first
FLASH = generator("Flash", "generators/SolidColor.fs", {"param/color/r": 1, "param/color/g": 1, "param/color/b": 1, "param/color/a": 1}, opacity=0)
SCAN = generator("White Lines · bars", "generators/Lines.fs", {"spacing": 0.14, "line_width": 0.006, "angle": 0.0, "shift": 0.0}, opacity=0)
WEB = generator("White Lines · web", "generators/Random Lines.fs", {"lineCount": 26, "lineWidth": 0.0016, "randomSeed": 1.0, "wobbleAmount": 0.25, "hueRange": 0.0, "colorSaturation": 0.0}, opacity=0)
DOTS = generator("White Points", "generators/Particles.fs", {"count": 900, "emitShape": 1, "life": 2.4, "speed": 0.2, "spread": 1.0, "turbulence": 0.6, "gravity": 0.0}, opacity=0)

# ---- The scene
SCENE = add({"id": new_id(), "name": "Stage", "enable": True, "locked": False, "viewports": {}, "group": True, "scene3d": True, "collapsed": False, "source": {"type": "none"}, "fx": [],
             "params": {"opacity": 1, "blend_mode": "normal", "camera/distance": 8.2, "camera/fov": 42, "camera/pitch": 11, "camera/yaw": 20, "camera/target/x": 0, "camera/target/y": 0.5, "camera/target/z": 0,
                        "scene/background/r": 0, "scene/background/g": 0, "scene/background/b": 0, "scene/background/a": 1, "scene/studio": 0.8, "scene/ambient_intensity": 0.05, "scene/exposure": 1,
                        "scene/ao": 1, "scene/reflections": 1, "scene/glow": 0.7, "scene/glow_threshold": 0.8, "scene/glow_size": 0.7, "scene/contrast": 1.08, "scene/saturation": 1, "scene/temperature": 0, "scene/vignette": 0.45,
                        "scene/fog/density": 0.16, "scene/fog/base": -1, "scene/fog/falloff": 0.22, "scene/fog/reach": 26, "scene/fog/color/r": 0.0, "scene/fog/color/g": 0.0, "scene/fog/color/b": 0.0}})
# Their pictures are under the scene, which is opaque: they only push the models' points
NOISE_FAST_AT, NOISE_SLOW_AT = len(layers), None

CORE = group3d("Core", SCENE, (0, 0.9, 0))
SPIN = group3d("Spin", SCENE, (0, 0.9, 0))
ORBIT = group3d("Orbit", SCENE, (0, 0.9, 0))
LINES3D = group3d("Lines 3D", SCENE, (0, 0.9, 0), 0.7)
POINTS3D = group3d("Points 3D", SCENE, (0, 0.9, 0))

# Their ids first: the models refer to them, the noises come last in the list
NOISE_FAST = "91"
NOISE_SLOW = "92"

BALL = model("Noise Ball", "Sphere", CORE, scale=1.0, color=(0.95, 0.42, 0.12), metal=0.7, rough=0.28, detail=8, displace=NOISE_FAST, amount=0.3, smooth=True)
RING = model("Noise Ring", "Torus", SPIN, scale=2.0, color=(1, 1, 1), metal=1, rough=0.12, detail=7, displace=NOISE_FAST, amount=0.16, smooth=True, rotation=(90, 0, 0))
RING2 = model("Noise Ring 2", "Torus", SPIN, scale=2.8, color=(0.85, 0.1, 0.12), metal=0.6, rough=0.3, detail=7, displace=NOISE_SLOW, amount=0.2, smooth=True, rotation=(20, 0, 40))
SATELLITES = []
for k in range(4):
    a = k * math.pi / 2
    SATELLITES.append(model(f"Noise Cube {k + 1}", "Cube", ORBIT, position=(3.4 * math.cos(a), 0, 3.4 * math.sin(a)), scale=0.55, color=(0.9, 0.9, 0.95), metal=0.8, rough=0.3, detail=7,
                            displace=NOISE_FAST, amount=0.3))
FLOOR = model("Floor", "Plane", SCENE, position=(0, -2.0, 0), rotation=(-90, 0, 0), scale=26, color=(0.16, 0.17, 0.2), metal=0.55, rough=0.38, detail=8, displace=NOISE_SLOW, amount=0.05, mid=0.5)
# White lines and points, in the scene, lit by nothing: they glow
for k in range(18):
    a = 360.0 * k / 18
    r = math.radians(a)
    model(f"Line {k + 1}", "Cube", LINES3D, position=(4.2 * math.cos(r), 0.35 * math.sin(3 * r), 4.2 * math.sin(r)), rotation=(0, -a, 0), scale=1, axes=(3.0, 0.012, 0.012), color=(1, 1, 1), unlit=True)
GOLDEN = math.pi * (3 - math.sqrt(5))
for k in range(36):
    y = 1 - 2 * (k + 0.5) / 36
    q = math.sqrt(1 - y * y)
    th = GOLDEN * k
    model(f"Point {k + 1}", "Sphere", POINTS3D, position=(3.0 * q * math.cos(th), 3.0 * y, 3.0 * q * math.sin(th)), scale=0.035, color=(1, 1, 1), unlit=True)

# The lights: on paths, looking at the ball, seen in the fog
SPOT_A = light("Spot White", SCENE, "spot", (0, 5.2, 0), (1, 0.96, 0.9), 70, look=(0, 0.9, 0), path=(1, 7, 7, 0.09), angle=16, falloff=0.35, soft=3, rays=1.4)
SPOT_B = light("Spot Red", SCENE, "spot", (0, 4.2, 0), (1, 0.08, 0.1), 30, look=(0, 0.2, 0), path=(2, 9, 6, -0.13), angle=20, falloff=0.45, soft=3, rays=1.6)
SPOT_C = light("Spot Blue", SCENE, "spot", (-4, 6, -3), (0.25, 0.45, 1), 40, look=(0, 0, 0), path=(1, 4, 4, 0.2), angle=13, falloff=0.3, soft=2, rays=1.6)
PANEL = light("Panel", SCENE, "area", (0, 5.8, 0), (0.9, 0.95, 1), 28, size=(6, 3), rays=0, pitch=-89.5, yaw=0, soft=2)

# The hits: empty layers, what the timelines write and the interactions read
HIT = {name: empty(f"Hit · {name.capitalize()}") for name in ("kick", "snare", "hat", "stab", "impact")}

# Black under the scene, as it fades at the end; the noises under that
BLACK = generator("Black", "generators/SolidColor.fs", {"param/color/r": 0, "param/color/g": 0, "param/color/b": 0, "param/color/a": 1}, blend="normal")
# The noises, under the opaque scene
layers.append({"id": NOISE_FAST, "name": "Noise · fast", "enable": True, "locked": False, "viewports": {}, "fx": [],
               "source": {"type": "isf", "path": "shaders/Simplex Noise.fs", "relative_path": "shaders/Simplex Noise.fs", "width": 1024, "height": 512},
               "params": {"opacity": 1, "blend_mode": "normal", "param/scale": 3.2, "param/octaves": 3, "param/speed": 0.9, "param/contrast": 1.4, "param/seed": 3}})
layers.append({"id": NOISE_SLOW, "name": "Noise · slow", "enable": True, "locked": False, "viewports": {}, "fx": [],
               "source": {"type": "isf", "path": "shaders/Simplex Noise.fs", "relative_path": "shaders/Simplex Noise.fs", "width": 1024, "height": 1024},
               "params": {"opacity": 1, "blend_mode": "normal", "param/scale": 3, "param/octaves": 4, "param/speed": 0.28, "param/contrast": 1.2, "param/seed": 11}})
shutil.copy2(REPO / "isf/generators/Simplex Noise.fs", SHOW / "shaders/Simplex Noise.fs")
shutil.copy2(REPO / "isf/generators/Particles.vs", SHOW / "shaders/Particles.vs")
SOUND = "90"
layers.append({"id": SOUND, "name": "Soundtrack", "enable": True, "locked": False, "viewports": {}, "fx": [], "play": False,
               "source": {"type": "audio", "path": AUDIO, "relative_path": AUDIO}, "params": {"play": False, "play_mode": "oneshot", "volume": 1}})

# ---- A hit becomes envelopes: on what, from what to what, how fast it rises and falls
ENVELOPES = [
    ("kick", BALL, "model/displace/amount", 0.3, 1.25, 0.004, 0.42),
    ("kick", BALL, "model/scale/all", 1.0, 1.42, 0.004, 0.24),
    ("kick", RING, "model/scale/all", 2.0, 2.45, 0.006, 0.3),
    ("kick", RING2, "model/scale/all", 2.8, 3.25, 0.01, 0.38),
    ("kick", RING, "model/displace/amount", 0.16, 0.6, 0.004, 0.3),
    ("kick", FLOOR, "model/displace/amount", 0.05, 0.17, 0.006, 0.5),
    ("kick", SPOT_A, "light/intensity", 70, 520, 0.002, 0.22),
    ("kick", SCENE, "scene/glow", 0.7, 2.6, 0.002, 0.2),
    ("kick", SCENE, "camera/fov", 42, 49, 0.002, 0.28),
    ("kick", DOTS, "opacity", 0, 0.7, 0.002, 0.16),
    ("snare", RING2, "model/bend", 0, 170, 0.005, 0.32),
    ("snare", BALL, "model/bend", 0, -60, 0.005, 0.22),
    ("snare", SPOT_B, "light/intensity", 30, 620, 0.002, 0.2),
    ("snare", SPOT_C, "light/intensity", 40, 420, 0.003, 0.3),
    ("snare", LINES3D, "merge/scale/all", 0.7, 1.4, 0.003, 0.2),
    ("snare", SCAN, "opacity", 0, 0.85, 0.002, 0.16),
    ("hat", POINTS3D, "merge/scale/all", 1, 1.14, 0.002, 0.07),
    ("hat", WEB, "opacity", 0, 0.34, 0.002, 0.07),
    ("stab", WEB, "param/wobbleAmount", 0.25, 1, 0.004, 0.3),
    ("stab", SCAN, "param/line_width", 0.006, 0.05, 0.004, 0.22),
    ("stab", SPOT_C, "light/angle", 13, 34, 0.01, 0.35),
    ("impact", FLASH, "opacity", 0, 1, 0.002, 0.7),
    ("impact", SCENE, "scene/exposure", 1, 3.2, 0.002, 1.1),
    ("impact", SCENE, "scene/temperature", 0, -0.7, 0.002, 1.2),
    ("impact", LINES3D, "merge/position/y", 0.9, 2.2, 0.002, 0.9),
]
interactions = []
for n, (hit, layer, param, lo, hi, attack, decay) in enumerate(ENVELOPES, 1):
    interactions.append({"id": str(n), "name": f"{hit.capitalize()} → {param}", "enabled": True, "source": "internal", "source_layer": HIT[hit], "source_param": "opacity",
                         "layer": layer, "param": param, "master": 1, "takeover": "jump",
                         "filters": [{"type": "envelope", "a": attack, "b": decay, "c": 0, "d": 0.02}, {"type": "remap", "a": 0, "b": 1, "c": lo, "d": hi}]})

# ---- The timelines: the hits of each section, on its own clock so nothing drifts; what turns all the time
SECTIONS = [("Intro", 0, 4), ("Drop", 4, 8), ("Break", 8, 10), ("Drop 2", 10, 14), ("Drop 2 b", 14, 18), ("Outro", 18, 20), ("End", 20, 22)]
timelines = []


def pulses(hits, start, end):
    """A value for 45 ms at each hit, at its velocity: what an envelope takes as its trigger."""
    keys = [[0, 0, "hold"]]
    for t, vel in hits:
        if start <= t < end:
            keys.append([round(t - start, 4), round(float(vel), 3), "hold"])
            keys.append([round(t - start + 0.045, 4), 0, "hold"])
    return keys


for name, a, b in SECTIONS:
    start, end = a * BAR, b * BAR
    tracks = []
    for hit, cues in (("kick", CUES["kick"]), ("snare", CUES["snare"]), ("hat", CUES["hat"] + CUES["open_hat"]), ("stab", CUES["stab"]), ("impact", CUES["impact"])):
        keys = pulses(sorted(cues), start, end)
        if len(keys) > 1:
            tracks.append({"layer": HIT[hit], "param": "opacity", "keys": keys})
    # Each bar the white bars turn; each stab the web changes
    tracks.append({"layer": SCAN, "param": "param/angle", "keys": [[round(i * BAR / 2, 4), (i * 37) % 180 if name != "Intro" else 0, "hold"] for i in range((b - a) * 2)]})
    tracks.append({"layer": WEB, "param": "param/randomSeed", "keys": [[round(i * BAR / 8, 4), 1 + i * 3 + a * 17, "hold"] for i in range((b - a) * 8)]})
    timelines.append({"id": str(len(timelines) + 1), "name": f"Hits · {name}", "duration": round(end - start, 4), "loop": "once", "tracks": tracks})
TL_HITS = {name: str(i + 1) for i, (name, _, _) in enumerate(SECTIONS)}


def sine(layer, param, period, center, amount):
    return {"layer": layer, "param": param, "oscillator": {"wave": "sine", "period": period, "center": center, "amplitude": amount}}


TURN_ID = str(len(timelines) + 1)
timelines.append({"id": TURN_ID, "name": "Turning", "duration": round(8 * BAR, 4), "loop": "loop", "tracks": [
    {"layer": SCENE, "param": "camera/yaw", "keys": [[0, -30], [round(8 * BAR, 4), 330, 0]]},
    sine(SCENE, "camera/pitch", 6 * BAR, 11, 9),
    sine(SCENE, "camera/target/y", 4 * BAR, 0.7, 0.4),
    {"layer": SPIN, "param": "merge/rotation/y", "keys": [[0, 0], [round(4 * BAR, 4), 360, 0]]},
    {"layer": SPIN, "param": "merge/rotation/x", "keys": [[0, 0], [round(8 * BAR, 4), 360, 0]]},
    {"layer": ORBIT, "param": "merge/rotation/y", "keys": [[0, 360], [round(2 * BAR, 4), 0, 0]]},
    {"layer": ORBIT, "param": "merge/rotation/z", "keys": [[0, 0], [round(8 * BAR, 4), 360, 0]]},
    {"layer": POINTS3D, "param": "merge/rotation/y", "keys": [[0, 0], [round(8 * BAR, 4), -360, 0]]},
    {"layer": LINES3D, "param": "merge/rotation/y", "keys": [[0, 0], [round(8 * BAR, 4), 360, 0]]},
]})


# ---- The sections: what each one changes, over how long
def values(*rows):
    return [{"layer": l, "path": p, "value": v, "time": t} for l, p, v, t in rows]


def preset(pid, name, fade, rows):
    return {"id": pid, "name": name, "fade": fade, "values": values(*rows), "animations": [], "interactions": [], "geometry": {}, "categories": {}}


GROUPS = {"lines": LINES3D, "points": POINTS3D, "orbit": ORBIT}
INTRO = {
    (SCENE, "camera/distance"): 12, (SCENE, "scene/fog/density"): 0.3, (SCENE, "scene/saturation"): 0, (SCENE, "scene/studio"): 0.2, (SCENE, "scene/vignette"): 0.7,
    (SPOT_B, "light/level"): 0, (PANEL, "light/level"): 0, (LINES3D, "enable"): False, (ORBIT, "enable"): False, (POINTS3D, "enable"): True, (RING2, "enable"): False,
}
presets = [
    preset("1", "Intro · rising", 9.0, [(SCENE, "camera/distance", 8.5, 9.0), (SCENE, "scene/fog/density", 0.16, 9.0), (SPOT_B, "light/level", 0.5, 9.0), (SCENE, "scene/studio", 0.5, 9.0)]),
    preset("2", "Drop", 0.0, [(SCENE, "camera/distance", 7.2, 0.0), (SCENE, "scene/fog/density", 0.1, 0.0), (SCENE, "scene/saturation", 1, 0.0), (SCENE, "scene/studio", 0.8, 0.0), (SCENE, "scene/vignette", 0.45, 0.0),
                              (SPOT_B, "light/level", 1, 0.0), (PANEL, "light/level", 1, 0.0), (LINES3D, "enable", True, 0.0), (RING2, "enable", True, 0.0)]),
    preset("3", "Break", 0.0, [(SCENE, "camera/distance", 5.6, 3.0), (SCENE, "scene/fog/density", 0.34, 0.0), (SCENE, "scene/saturation", 0, 0.0), (PANEL, "light/level", 0.15, 0.0), (SPOT_B, "light/level", 0, 0.0), (RING2, "enable", False, 0.0),
                               (NOISE_FAST, "param/scale", 9, 2.5), (NOISE_FAST, "param/speed", 2, 0.0)]),
    preset("4", "Drop 2", 0.0, [(SCENE, "camera/distance", 8.0, 0.0), (SCENE, "scene/fog/density", 0.09, 0.0), (SCENE, "scene/saturation", 1.15, 0.0), (SCENE, "scene/contrast", 1.2, 0.0), (SCENE, "scene/glow", 1, 0.0),
                                (PANEL, "light/level", 1, 0.0), (SPOT_B, "light/level", 1, 0.0), (RING2, "enable", True, 0.0), (ORBIT, "enable", True, 0.0), (NOISE_FAST, "param/scale", 3.2, 0.0), (NOISE_FAST, "param/speed", 1.2, 0.0)]),
    preset("5", "Drop 2 · closer", 3.0, [(SCENE, "camera/distance", 6.2, 3.0), (SCENE, "scene/temperature", -0.2, 3.0), (NOISE_FAST, "param/scale", 5, 3.0)]),
    preset("6", "Outro", 0.0, [(SCENE, "camera/distance", 14, 6.0), (SCENE, "scene/fog/density", 0.32, 6.0), (SCENE, "scene/saturation", 0, 4.0), (ORBIT, "enable", False, 0.0), (RING2, "enable", False, 0.0),
                               (LINES3D, "enable", False, 0.0), (NOISE_FAST, "param/speed", 0.25, 6.0)]),
    preset("7", "End", 0.0, [(SCENE, "opacity", 0, 4.5), (POINTS3D, "enable", True, 0.0)]),
]

# ---- The first state (a snapshot): all of the above, and the soundtrack started
states = []
for layer in layers:
    state = copy.deepcopy(layer)
    state["included"] = True
    for (lid, path), value in INTRO.items():
        if lid == state["id"]:
            if path == "enable":
                state["enable"] = value
            else:
                state["params"][path] = value
    if state["id"] == SOUND:
        state["params"] = {"restart": True, "play": True, "play_mode": "oneshot", "volume": 1}
    states.append(state)
snapshot = {"id": "1", "name": "Ready", "fade": 0, "layers": states, "composition": {"included": True, "opacity": 1, "volume": 1},
            "interactions": [{"id": i["id"], "included": True, "enabled": True} for i in interactions]}

# ---- The steps, on the soundtrack's timecode
steps = [{"text": "GO: the soundtrack starts", "snapshot": "1", "description": "All of it plays by itself from here"},
         {"timeline": TURN_ID, "action": "play", "text": "Turning", "at": 0.01},
         {"timeline": TL_HITS["Intro"], "action": "play", "text": "Hits · Intro", "at": 0.02},
         {"preset": "1", "text": "Intro: it rises", "at": 0.03}]
WHEN = {"Drop": "2", "Break": "3", "Drop 2": "4", "Drop 2 b": "5", "Outro": "6", "End": "7"}
for name, a, b in SECTIONS[1:]:  # Each on the clock by itself: a step that follows would wait for what is before it to end
    at = round(a * BAR, 3)
    steps.append({"text": name, "preset": WHEN[name], "at": max(0, at - 0.012)})
    steps.append({"text": f"{name}: its hits", "timeline": TL_HITS[name], "action": "play", "at": at})
project = {"app": "NyxMap", "format_version": 1, "composition": {"width": W, "height": H, "fps": 60, "speed": 1}, "audio": {"volume": 1}, "layers": layers, "interactions": interactions,
           "timelines": timelines, "presets": presets, "snapshots": [snapshot], "snapshot_tree": ["1"],
           "sequences": [{"id": "1", "name": "Trip Hop Drop · on the soundtrack's timecode", "loop": False, "steps": steps, "timecode": {"layer": SOUND, "param": "playhead"}}],
           "current_sequence": 0, "startup_sequence": 0, "startup_step": 0, "startup_go": "--autoplay" in sys.argv, "ui": {"selected_layer": 0}}
(SHOW / "Trip Hop Drop.nyxmap").write_text(json.dumps(project, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
print(f"{len(layers)} layers, {len(interactions)} envelopes, {len(timelines)} timelines, {sum(len(t['keys']) for tl in timelines for t in tl['tracks'] if 'keys' in t)} keys, {len(steps)} steps")
