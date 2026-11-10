declare name "Reticolo Rotto";
declare description "Stereo break bass instrument for NyxMap's timeline";

import("stdfaust.lib");

kickLevel = hslider("Kick Level", 0.82, 0, 1, 0.01) : si.smoo;
bassLevel = hslider("Bass Level", 0.78, 0, 1, 0.01) : si.smoo;
bassDrive = hslider("Bass Drive", 0.65, 0, 1, 0.01) : si.smoo;
width = hslider("Width", 0.68, 0, 1, 0.01) : si.smoo;
drumLevel = hslider("Break Level", 0.74, 0, 1, 0.01) : si.smoo;

kickNote = hslider("Kick", 0, -1, 1, 0.001);
snareNote = hslider("Snare", 0, -1, 1, 0.001);
hatNote = hslider("Hat", 0, -1, 1, 0.001);
hatLength = hslider("Hat Length", 0.043, 0.012, 0.28, 0.001);
metalNote = hslider("Metal", 0, -1, 1, 0.001);
bassNote = hslider("Bass", 0, -1, 1, 0.001);
pitch = hslider("Bass Pitch", 43.65, 28, 160, 0.01) : si.smooth(ba.tau2pole(0.015));
length = hslider("Bass Length", 0.2, 0.025, 1.4, 0.001);
bite = hslider("Bite", 0.64, 0, 1, 0.01) : si.smooth(ba.tau2pole(0.026));
airNote = hslider("Air", 0, -1, 1, 0.001);

// Alternating velocity signs let consecutive notes retrigger even at the same level.
strike(x) = (x != x') & (x != 0);
age(trig) = (+(1) : min(8 * ma.SR) : *(1 - trig)) ~ _ : /(float(ma.SR));
vel(x) = abs(x) : ba.sAndH(strike(x));
fadeIn(t, seconds) = min(1, t / seconds);
sine(p) = sin(2 * ma.PI * p);
phase(f, trig) = os.lf_sawpos_reset(f, trig);
sat(x) = ma.tanh(x);
noise = no.noise;

kt = age(strike(kickNote));
kv = vel(kickNote);
kp = phase(46 + 132 * exp(-kt / 0.014) + 23 * exp(-kt / 0.051), strike(kickNote));
kickEnv = fadeIn(kt, 0.0018) * exp(-kt / 0.087);
kickSub = 0.87 * sat(1.5 * sine(kp) + 0.12 * sine(2 * kp)) * kickEnv;
kickKnock = 0.22 * os.hs_oscsin(176 + 235 * exp(-kt / 0.009), strike(kickNote))
    * fadeIn(kt, 0.001) * exp(-kt / 0.025);
kickClick = 0.14 * (noise : fi.bandpass(2, 900, 4600)) * exp(-kt / 0.010) * fadeIn(kt, 0.0008);
kickRoom = kickKnock + kickClick;
kickStereo = ((kickRoom : @(int(ma.SR * 0.006))) - (kickRoom : @(int(ma.SR * 0.011)))) * 0.46 * width;

// Duck the bass under the kick, leaving its stereo harmonics out of the lowest octave.
duck = 1 - 0.79 * kv * exp(-kt / 0.071);
bt = age(strike(bassNote));
bv = vel(bassNote);
bl = length : ba.sAndH(strike(bassNote));
be = fadeIn(bt, 0.009) * exp(-1 * max(0, bt - bl) / 0.035);
bp = os.lf_sawpos(pitch);
sub = 0.61 * sine(bp) + 0.09 * sine(2 * bp);
wobble = 0.77 + 0.23 * os.osc(5.333333);
fm = (0.34 + 1.2 * bite + 0.8 * bassDrive) * (0.54 + exp(-bt / 0.13)) * wobble;
mid(p) = 0.45 * sine(2 * p + 0.22 * fm * sine(3 * p)) + 0.31 * sine(3 * p + 0.15 * fm * sine(p))
    + 0.13 * sine(5 * p + 0.12 * fm * sine(2 * p));
reeseVoice(p) = mid(p) : *(1.5 + 2.3 * bassDrive) : sat : fi.highpass(2, 125) : fi.lowpass(2, 610 + 1450 * bite);
reeseL = reeseVoice(os.lf_sawpos(pitch * 0.993));
reeseR = reeseVoice(os.lf_sawpos(pitch * 1.009));
bassCenter = sub + 0.27 * (reeseL + reeseR);
bassSide = 0.40 * (reeseL - reeseR) * width;
bassGain = be * bv * duck * bassLevel * 0.83;

st = age(strike(snareNote));
sv = vel(snareNote);
snareBody = 0.46 * os.hs_oscsin(178 + 92 * exp(-st / 0.021), strike(snareNote)) * exp(-st / 0.041)
    + 0.13 * os.hs_oscsin(352, strike(snareNote)) * exp(-st / 0.028);
snareNoise = (noise : fi.bandpass(2, 770, 7600)) * (0.81 * exp(-st / 0.057) + 0.38 * exp(-st / 0.011));
snare = fadeIn(st, 0.0014) * sv * (snareBody + snareNoise);
snareSide = ((snare : @(int(ma.SR * 0.019))) - (snare : @(int(ma.SR * 0.031)))) * width * 0.25;

ht = age(strike(hatNote));
hv = vel(hatNote);
hl = hatLength : ba.sAndH(strike(hatNote));
hat = (noise + 0.26 * os.osc(7123) * os.osc(4139)) : fi.highpass(2, 6050)
    : *(fadeIn(ht, 0.001) * exp(-ht / max(0.012, hl)) * hv * 0.44);
hatSide = (hat : @(int(ma.SR * 0.003))) * width * 0.43 * ma.signum(hatNote);

mt = age(strike(metalNote));
mv = vel(metalNote);
mp = phase(612 + 193 * exp(-mt / 0.037), strike(metalNote));
metal = sine(mp + 0.41 * sine(mp * 2.43)) * exp(-mt / 0.068) * fadeIn(mt, 0.003) * mv * 0.25;
metalSide = ((metal : @(int(ma.SR * 0.015))) - (metal : @(int(ma.SR * 0.039)))) * width * 0.62;

at = age(strike(airNote));
av = vel(airNote);
airEnv = fadeIn(at, 0.07) * exp(-at / 0.29) * av;
air = ((noise : fi.bandpass(2, 350, 1700)) + 0.21 * os.osc(311 + 188 * at) * os.osc(476))
    * airEnv * 0.34;
airSide = (air : @(int(ma.SR * 0.026))) * width * 0.63;

center = (kickSub + kickRoom) * kv * kickLevel + bassCenter * bassGain
    + (snare + hat + metal + air) * drumLevel;
side = kickStereo * kv * kickLevel + bassSide * bassGain
    + (snareSide + hatSide + metalSide + airSide) * drumLevel;

limit = fi.highpass(2, 24) : *(1.29) : sat : *(0.93);
process = (center + side : limit), (center - side : limit);
