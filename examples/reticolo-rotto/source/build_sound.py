"""Editable break bass patterns for the Reticolo Rotto Faust instrument."""

from collections import defaultdict

BPM = 160
BARS = 32
STEP = 60 / BPM / 4
BAR = 16 * STEP
AUDIO = "102"
SOUND = "audio/Reticolo Rotto.dsp"

KICKS = (
    (0, 3.5, 7, 10.5, 14),
    (0, 6, 9.5, 15),
    (0, 2.5, 6, 10, 13.75),
    (0, 5, 8.75, 12.5, 15),
)
# Step, length in sixteenths, MIDI pitch and velocity. Sustain the root between jabs.
RIFF = (
    ((.36, 2.85, 29, .96), (4.5, 1.73, 29, .84), (7.33, 2.72, 32, .89), (11.13, 1.44, 28, .75), (14.4, 1.43, 29, .92)),
    ((.35, 3.91, 29, 1), (6.38, 1.71, 36, .80), (9.85, 2.5, 31, .89), (13.5, 1.22, 28, .72)),
    ((.33, 1.67, 29, .91), (2.88, 2.34, 29, .87), (6.35, 1.53, 32, .87), (8.62, 2.66, 28, .94), (13.95, 1.67, 34, .76)),
    ((.39, 3.41, 29, 1), (5.36, 1.37, 36, .76), (8.98, 1.93, 28, .93), (11.63, 1.29, 32, .87), (13.21, 2.32, 27, .82)),
)
HALF = ((.4, 6.5, 29, 1), (8.62, 2.28, 28, .93), (11.89, 2.9, 34, .77))


def hz(note):
    return round(440 * 2 ** ((note - 69) / 12), 4)


def sound_tracks(first_bar, next_bar):
    """Make gate, pitch and decay tracks that a cue can play independently."""
    tracks = defaultdict(dict)
    triggers = ("kick", "snare", "hat", "metal", "bass", "air")
    duration = (next_bar - first_bar) * BAR

    def key(param, time, value):
        tracks[AUDIO, f"synth/{param}"][round(time, 5)] = (value, "hold")

    # Set note parameters before their triggers in the timeline's track order.
    for name, value in {"bass_pitch": hz(29), "bass_length": .2, "bite": .64, "hat_length": .043}.items():
        key(name, 0, value)
    for name in triggers:
        key(name, 0, 0)
        key(name, duration - .001, 0)

    def at(bar, step):
        # Give the DSP one frame at the start of every cue to clear its trigger controls.
        return (bar - first_bar) * BAR + .025 + step * STEP

    def hit(name, bar, step, level):
        key(name, at(bar, step), level)

    def note(bar, step, steps, pitch, velocity, bite):
        t = at(bar, step)
        key("bass_pitch", t, hz(pitch))
        key("bass_length", t, round(steps * STEP, 4))
        key("bite", t, bite)
        hit("bass", bar, step, velocity)

    def hat(bar, step, velocity, length):
        key("hat_length", at(bar, step), length)
        hit("hat", bar, step, velocity)

    for bar in range(first_bar, next_bar):
        variant = bar % len(RIFF)
        if bar < 4 or 20 <= bar < 24 or bar >= 30:
            if bar in (0, 2, 20, 22, 30):
                hit("kick", bar, 0, .87 if bar < 4 else 1)
                note(bar, .46, 5 if bar == 30 else 4.2, 29 if bar != 20 else 28, .79, .34)
            if bar == 31:
                continue
            hit("air", bar, .15, .77 if bar < 30 else .38)
            for step in (3.2, 7.15, 9.11, 13.14, 15.14):
                hat(bar, step, .31 if step < 10 else .23, .043)
            hit("metal", bar, 7.15, .52)
            if bar in (3, 23):
                for index, step in enumerate((10, 12, 13.15, 14, 14.5, 15)):
                    hit("snare", bar, step, .25 + .11 * index)
            continue

        half_time = 10 <= bar < 14
        limit = 10 if bar in (13, 19, 29) else 12 if bar == 9 else 16
        for index, step in enumerate((0, 7, 11.5) if half_time else KICKS[variant]):
            if step < limit:
                hit("kick", bar, step, 1 if index == 0 else (.83, .95, .91, .86)[index - 1])
        for step in ((8,) if half_time else (4, 12)):
            if step < limit:
                hit("snare", bar, step, .94 if half_time else .88)
        for index, step in enumerate((2, 5.6, 14.15) if half_time else (3.15, 6.5, 11.16, 14.75)):
            if step < limit:
                hit("snare", bar, step, (.31, .24, .40, .21)[index])
        for step in range(limit):
            if step % 2 and (step + variant) % 5 == 0:
                continue
            hat(bar, step + (.16 if step % 2 else 0), .63 if step % 4 == 2 else .39,
                .118 if step == 6 else .038 if step % 2 else .048)
        for step in (2.75, 10.75) if variant % 2 == 0 else (6.75, 15.45):
            if step < limit:
                hit("metal", bar, step, .47)
        if limit < 16:
            for step in (limit - .65, limit - .13):
                hit("metal", bar, step, .39)
        color = .47 if half_time else .87 if bar >= 24 else .67
        for index, (step, length, pitch, velocity) in enumerate(HALF if half_time else RIFF[variant]):
            if step < limit - .62:
                note(bar, step, min(length, limit - step - .28), pitch, velocity,
                     min(1, color + .1 * (index % 2)))
    for name in triggers:
        points = tracks[AUDIO, f"synth/{name}"]
        strikes = (time for time, (value, _) in sorted(points.items()) if value > 0)
        for index, time in enumerate(strikes):
            if index % 2:
                value, curve = points[time]
                points[time] = (-value, curve)
    return tracks
